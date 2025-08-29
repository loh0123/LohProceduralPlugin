// Copyright (c) 2025 Loh Zhi Kang ( loh0123@hotmail.com )
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LPPChunkManagerSubsystem.generated.h"

class ULFPGridTagDataComponent;
class ULPPChunkController;

USTRUCT ( BlueprintType )
struct FLPPLoadedChunkData
{
	GENERATED_BODY ( )

public:

	UPROPERTY ( )
	TObjectPtr < AActor > ChunkActor = nullptr;

	UPROPERTY ( )
	TArray < TObjectPtr < AActor > > LoaderList = TArray < TObjectPtr < AActor > > ( );
};

/**
 * 
 */
UCLASS ( )
class LOHPROCEDURALPLUGIN_API ULPPChunkManagerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY ( )

public:

	UFUNCTION ( BlueprintCallable , Category = "Default" )
	void SetupChunkManager ( const TArray < ULFPGridTagDataComponent* >& NewDataComponentList , const TSubclassOf < AActor > NewChunkActorClass , const FVector& NewSpawnOffset , const FVector& ChunkDataSize );

public:

	UFUNCTION ( BlueprintCallable , Category = "Default" )
	FVector GetChunkLocation ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex ) const;

	UFUNCTION ( BlueprintPure , Category = "Default" )
	ULFPGridTagDataComponent* GetDataComponent ( const int32 ComponentIndex ) const;

public:

	UFUNCTION ( BlueprintCallable , Category = "Default" )
	AActor* LoadChunk ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex , AActor* LoaderActor );

	UFUNCTION ( BlueprintCallable , Category = "Default" )
	bool UnloadChunk ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex , AActor* LoaderActor );

public:

	UFUNCTION ( BlueprintCallable , meta=(AutoCreateRefTerm="GridDataIndexList") , Category = "Default" )
	void RequestChunkUpdate ( const int32 ComponentIndex , const TArray < FIntVector >& GridDataIndexList , const bool bIsolateRegion );

protected:

	UFUNCTION ( )
	AActor* AllocateChunkActor ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex );

protected:

	UPROPERTY ( Transient )
	TMap < FIntVector , FLPPLoadedChunkData > LoadedChunkMap;

	UPROPERTY ( Transient )
	TArray < TObjectPtr < AActor > > AvailableChunkList;

	UPROPERTY ( Transient )
	TArray < TObjectPtr < ULFPGridTagDataComponent > > DataComponentList;

	UPROPERTY ( Transient )
	TArray < float > ComponentHeightList;

	UPROPERTY ( Transient )
	TArray < FVector > ComponentChunkGapList;

protected:

	UPROPERTY ( Transient )
	TSubclassOf < AActor > ChunkActorClass = nullptr;

	UPROPERTY ( Transient )
	FVector SpawnOffset = FVector ( 0.0f , 0.0f , 0.0f );
};
