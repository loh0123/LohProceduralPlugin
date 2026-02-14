// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "LPPChunkActorInterface.generated.h"

// This class does not need to be modified.
UINTERFACE ( )
class ULPPChunkActorInterface : public UInterface
{
	GENERATED_BODY ( )
};

/**
 * Chunk Actor Interface
 * - The actor that implements this should not replicate
 * - Chunk manager subsystems spawned actors only
 */
class LOHPROCEDURALPLUGIN_API ILPPChunkActorInterface
{
	GENERATED_BODY ( )

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:

	UFUNCTION ( BlueprintNativeEvent , Category=Default )
	void OnChunkIDChanged ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex );

	virtual void OnChunkIDChanged_Implementation ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex )
	{
	};

	// Only on owing client
	UFUNCTION ( BlueprintNativeEvent , Category=Default )
	void OnLODChanged ( const int32 LODIndex );

	virtual void OnLODChanged_Implementation ( const int32 LODIndex )
	{
	};

	UFUNCTION ( BlueprintNativeEvent , Category=Default )
	void OnRequestChunkUpdate ( const TArray < int32 >& DataIndexList , const bool bIsMetaUpdate );

	virtual void OnRequestChunkUpdate_Implementation ( const TArray < int32 >& DataIndexList , const bool bIsMetaUpdate )
	{
	};
};
