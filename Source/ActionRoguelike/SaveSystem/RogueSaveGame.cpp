// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueSaveGame.h"

#include "ActionRoguelike.h"
#include "GameFramework/PlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RogueSaveGame)


FPlayerSaveData* URogueSaveGame::GetPlayerData(APlayerState* PlayerState)
{
	check(PlayerState);

	// Will not give unique ID while PIE so we skip that step while testing in editor.
	// UObjects don't have access to UWorld, so we grab it via PlayerState instead
	if (PlayerState->GetWorld()->IsPlayInEditor())
	{
		UE_LOGFMT(LogGame, Log, "During PIE we cannot use PlayerID to retrieve Saved Player data. Using first entry in array if available.");

		if (SavedPlayers.IsValidIndex(0))
		{
			return &SavedPlayers[0];
		}

		// No saved player data available
		return nullptr;
	}

	// Easiest way to deal with the different IDs is as FString (original Steam id is uint64)
	// Keep in mind that GetUniqueId() returns the online id, where GetUniqueID() is a function from UObject (very confusing...)
	FString PlayerID = PlayerState->GetUniqueId().ToString();
	// Iterate the array and match by PlayerID (eg. unique ID provided by Steam)
	return SavedPlayers.FindByPredicate([&](const FPlayerSaveData& Data) { return Data.PlayerID == PlayerID; });
}
