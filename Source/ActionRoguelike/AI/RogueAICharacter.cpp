// Fill out your copyright notice in the Description page of Project Settings.


#include "AI/RogueAICharacter.h"

#include "ActionRoguelike.h"
#include "AIController.h"
#include "DrawDebugHelpers.h"
#include "BrainComponent.h"
#include "NiagaraComponent.h"
#include "SharedGameplayTags.h"
#include "SignificanceManager.h"
#include "UI/RogueWorldUserWidget.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ActionSystem/RogueActionComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Components/AudioComponent.h"
#include "Components/CanvasPanel.h"
#include "Perception/AISense_Damage.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RogueAICharacter)


ARogueAICharacter::ARogueAICharacter()
{
	ActionComp = CreateDefaultSubobject<URogueActionComponent>(TEXT("ActionComp"));
	// Set some defaults, ideally we handle this through some data asset instead
	ActionComp->SetDefaultAttributeSet(FRogueMonsterAttributeSet::StaticStruct());

	AttackSoundComp = CreateDefaultSubobject<UAudioComponent>(TEXT("AttackAudioComp"));
	AttackSoundComp->SetupAttachment(RootComponent);
	AttackSoundComp->bAutoManageAttachment = true;
	AttackSoundComp->SetAutoActivate(false);

	// Default set up for the MinionRanged
	AttackFX_Socket = "Muzzle_Front";

	AttackParticleComp = CreateDefaultSubobject<UNiagaraComponent>(TEXT("AttackParticleComp"));
	AttackParticleComp->SetupAttachment(GetMesh(), AttackFX_Socket);
	AttackParticleComp->bAutoManageAttachment = true;
	AttackParticleComp->SetAutoActivate(false);

	// Ensures we receive a controlled when spawned in the level by our gamemode
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// Enabled on mesh to react to incoming projectiles
	USkeletalMeshComponent* SkelMesh = GetMesh();
	SkelMesh->SetGenerateOverlapEvents(true);
	// Skip performing overlap queries on the Physics Asset after animation (17 queries in case of our MinionRangedBP)
	SkelMesh->bUpdateOverlapsOnAnimationFinalize = false;

	// Skip bones when not visible, may miss anim notifies etc. if animation is skipped so these options must be tested per use case
	SkelMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;

	//TimeToHitParamName = "TimeToHit";
	HitFlash_CustomPrimitiveIndex = 0;

	SignificanceTag = "AICharacter";
}

void ARogueAICharacter::BeginPlay()
{
	Super::BeginPlay();

	{
		USignificanceManager* SigMan = USignificanceManager::Get(GetWorld());
		check(SigMan);

		// This function will run async from the GameThread, make sure it's threadsafe
		auto SignificanceFunc = [](USignificanceManager::FManagedObjectInfo* ObjectInfo, const FTransform& Viewpoint) -> float
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CalculateSignificance)
			
			/*AActor* MyActor = CastChecked<AActor>(ObjectInfo->GetObject());
			check(IsValid(MyActor));
			
			float DistanceSqrt = (MyActor->GetActorLocation() - Viewpoint.GetLocation()).SizeSquared();
			
			if (MyActor->WasRecentlyRendered())
			{
				// while using negative sig values, use a <1.0 multiplier
				DistanceSqrt *= 0.5f;
			}*/

			USkeletalMeshComponent* SkelMeshComp = CastChecked<USkeletalMeshComponent>(ObjectInfo->GetObject());
			check(IsValid(SkelMeshComp));
			
			float DistanceSqrt = (SkelMeshComp->GetComponentLocation() - Viewpoint.GetLocation()).SizeSquared();
			
			if (SkelMeshComp->WasRecentlyRendered())
			{
				// while using negative sig values, use a <1.0 multiplier
				DistanceSqrt *= 0.5f;
			}

			// Note: AI can further define significance, for example,
			//			while in combat or having the player as a known target we could increase its significance

			// Negative distance to easily have larger distance mean lower significance
			return -DistanceSqrt;
		};

		// Instead of passing the entire Actor, we can pass the minimal data, such as the RootComponent, or SkeletalMeshComponent
		// This should allow us to be more cache efficient (from simple testing this does run slightly faster than using the Actor)
		SigMan->RegisterObject(GetMesh(), SignificanceTag, SignificanceFunc);
	}
}

void ARogueAICharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	{
		USignificanceManager* SigMan = USignificanceManager::Get(GetWorld());
		check(SigMan);
		SigMan->UnregisterObject(this);
	}
}


void ARogueAICharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// The "simplest" syntax compared to the other convoluted attempts
	FRogueAttribute* FoundAttribute = ActionComp->GetAttribute(SharedGameplayTags::Attribute_Health);
	FoundAttribute->OnAttributeChanged.AddUObject(this, &ThisClass::OnHealthAttributeChanged);
	
	// Cheap trick to disable until we need it in the health event
	CachedOverlayMaxDistance = GetMesh()->OverlayMaterialMaxDrawDistance;
	GetMesh()->SetOverlayMaterialMaxDrawDistance(1);
}


void ARogueAICharacter::OnHealthAttributeChanged(float NewValue, const FAttributeModification& AttributeModification)
{
	float Delta = AttributeModification.Magnitude;
	AActor* InstigatorActor = AttributeModification.Instigator.Get();
	
	if (Delta < 0.0f)
	{
		// Create once, and skip on instant kill
		if (ActiveHealthBar == nullptr && NewValue > 0.0)
		{
			ActiveHealthBar = CreateWidget<URogueWorldUserWidget>(GetWorld(), HealthBarWidgetClass);
			if (ActiveHealthBar)
			{
				ActiveHealthBar->AttachedActor = this;
				URogueWorldUserWidget::AddToRootCanvasPanel(ActiveHealthBar);
			}
		}

		// Replaces the above "old" method of requiring unique material instances for every mesh element on the player 
		GetMesh()->SetCustomPrimitiveDataFloat(HitFlash_CustomPrimitiveIndex, GetWorld()->TimeSeconds);

		// We can skip rendering this at a distance
		GetMesh()->SetOverlayMaterialMaxDrawDistance(CachedOverlayMaxDistance);

		// After 1.0seconds we should be finished with the hitflash (re-use the handle to reset timer if we get hit again)
		GetWorldTimerManager().SetTimer(OverlayTimerHandle, [this]()
		{
			// Cheap trick to skip rendering this all the time unless we are actively hit flashing
			GetMesh()->SetOverlayMaterialMaxDrawDistance(1);
		}, 1.0f, false);

		// Died
		if (NewValue <= 0.0f)
		{
			// stop BT
			if (HasAuthority())
			{
				AAIController* AIC = GetController<AAIController>();
				AIC->GetBrainComponent()->StopLogic("Killed");

				// Clears active actions, and (de)buffs.
				ActionComp->StopAllActions();
			}
			
			// ragdoll
			GetMesh()->SetAllBodiesSimulatePhysics(true);
			GetMesh()->SetCollisionProfileName(Collision::Ragdoll_ProfileName);

			GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			GetCharacterMovement()->DisableMovement();

			// set lifespan
			SetLifeSpan(10.0f);
		}
		else
		{
			UAISense_Damage::ReportDamageEvent(this, this, InstigatorActor, FMath::Abs(Delta),
				InstigatorActor->GetActorLocation(), GetActorLocation());
		}
	}
}


AActor* ARogueAICharacter::GetTargetActor() const
{
	AAIController* AIC = GetController<AAIController>();
	// Not guaranteed to be possessed (as we may use this in the AnimBP)
	if (AIC)
	{
		return Cast<AActor>(AIC->GetBlackboardComponent()->GetValueAsObject("TargetActor"));
	}
	return nullptr;
}


void ARogueAICharacter::MulticastPlayAttackFX_Implementation()
{
	AttackSoundComp->Play();

	AttackParticleComp->Activate(true);
	
	PlayAnimMontage(AttackMontage);
}


void ARogueAICharacter::SignificanceLODChanged(int32 NewLOD)
{
	// Set as 'dormant' if actor is hidden, otherwise we continue ticking the entire character
	// @todo: Not yet implemented, could use -1 as a "Hidden" special state
	//const bool bHiddenSignificance = (NewLOD == -1);
	//SetActorTickEnabled(!bHiddenSignificance);
	//GetCharacterMovement()->SetComponentTickEnabled(!bHiddenSignificance);

	UE_LOG(LogGame, Log, TEXT("Actor: %s, NewLOD: %i (Bucket)"), *GetName(), NewLOD);

	if (NewLOD == 0)
	{
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}
	// Anything beside best LOD
	else
	{
		GetCharacterMovement()->SetMovementMode(MOVE_NavWalking);
	}

/*
	EVisibilityBasedAnimTickOption AnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	switch (Significance)
	{
		// Example, force to always tick pose when really nearby. might need the pose even while offscreen
	case ESignificanceValue::Highest:
		AnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		break;
	case ESignificanceValue::Medium:
		AnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
		break;
	case ESignificanceValue::Lowest:
	case ESignificanceValue::Hidden:
	case ESignificanceValue::Invalid:
		AnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	}
	
	GetMesh()->VisibilityBasedAnimTickOption = AnimTickOption;
*/
}


FGenericTeamId ARogueAICharacter::GetGenericTeamId() const
{
	// Fetch from the AI Controller who has built-in TeamId
	return FGenericTeamId::GetTeamIdentifier(GetController());
}
