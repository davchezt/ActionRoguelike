// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
//#include "Animation/RogueCurveAnimSubsystem.h"
#include "GameFramework/Actor.h"
#include "Core/RogueGameplayInterface.h"
#include "RogueTreasureChest.generated.h"

class UNiagaraComponent;
class UStaticMeshComponent;

UCLASS()
class ACTIONROGUELIKE_API ARogueTreasureChest : public AActor, public IRogueGameplayInterface
{
	GENERATED_BODY()

public:

	virtual void Interact_Implementation(AController* InstigatorController) override;

	virtual void OnActorLoaded_Implementation() override;

protected:

	UPROPERTY(EditDefaultsOnly, Category= "Animation")
	UCurveFloat* LidAnimCurve;

	UPROPERTY(ReplicatedUsing="OnRep_LidOpened", BlueprintReadOnly, SaveGame)
	bool bLidOpened;

	void ConditionalOpenChest();

	UFUNCTION()
	void OnRep_LidOpened();

	//virtual void Tick(float DeltaSeconds) override;

	//FActiveCurveAnim* CurveAnimInst;

	UPROPERTY(VisibleAnywhere, Category=Components)
	TObjectPtr<UStaticMeshComponent> BaseMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Components)
	TObjectPtr<UStaticMeshComponent> LidMesh;

	UPROPERTY(VisibleAnywhere, Category=Components)
	TObjectPtr<UNiagaraComponent> OpenChestEffect;

	UPROPERTY(VisibleAnywhere, Category=Components)
	TObjectPtr<UAudioComponent> OpenChestSound;

public:	

	ARogueTreasureChest();
};
