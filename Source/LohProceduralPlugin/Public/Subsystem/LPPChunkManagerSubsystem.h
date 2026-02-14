// Copyright (c) 2025 Loh Zhi Kang ( loh0123@hotmail.com )
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "CoreMinimal.h"
#include "LPPChunkManagerSubsystem.generated.h"

class ULFPChunkedTagDataComponent;
class ULFPChunkedGridPositionComponent;
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

USTRUCT ( BlueprintType )
struct FLPPAsyncChunkManagerAction
{
	GENERATED_BODY ( )

public:

	UPROPERTY ( )
	TSet < int32 > UpdateDataIndexList = TSet < int32 > ( );

	UPROPERTY ( )
	bool bIsMetaUpdate = true;
};

/**
 * 
 */
UCLASS ( )
class LOHPROCEDURALPLUGIN_API ULPPChunkManagerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY ( )

public:

	virtual void Initialize ( FSubsystemCollectionBase& Collection ) override;

	virtual void Tick ( float DeltaTime ) override;

	virtual TStatId GetStatId ( ) const override;

public:

	UFUNCTION ( BlueprintCallable , Category = "Default" , meta=(AutoCreateRefTerm="NewSpawnOffset,ChunkDataSize") )
	void SetupChunkManager (
		const TArray < ULFPChunkedTagDataComponent* >&      NewDataComponentList ,
		const TArray < ULFPChunkedGridPositionComponent* >& NewPositionComponentList ,
		const TSubclassOf < AActor >                        NewChunkActorClass ,
		const FVector&                                      NewSpawnOffset ,
		const FVector&                                      ChunkDataSize ,
		const uint8                                         TargetFrame
		);

public:

	UFUNCTION ( BlueprintCallable , Category = "Default" )
	FVector GetChunkLocation ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex ) const;

	UFUNCTION ( BlueprintPure , Category = "Default" )
	ULFPChunkedTagDataComponent* GetDataComponent ( const int32 ComponentIndex ) const;

	UFUNCTION ( BlueprintPure , Category = "Default" )
	ULFPChunkedGridPositionComponent* GetPositionComponent ( const int32 ComponentIndex ) const;

public:

	UFUNCTION ( BlueprintCallable , Category = "Default" )
	void LoadRegion ( const int32 ComponentIndex , const int32 RegionIndex , AActor* LoaderActor );

public:

	UFUNCTION ( BlueprintCallable , Category = "Default" )
	AActor* LoadChunk ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex , AActor* LoaderActor );

	UFUNCTION ( BlueprintCallable , Category = "Default" )
	bool UnloadChunk ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex , AActor* LoaderActor );

public:

	UFUNCTION ( BlueprintCallable , meta=(AutoCreateRefTerm="GridDataIndexList") , Category = "Default" )
	void RequestChunkUpdate ( const int32 ComponentIndex , const TArray < FIntVector >& GridDataIndexList , const bool bIsMetaUpdate );

protected:

	UFUNCTION ( )
	void NotifyChunkLoad ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex ) const;

	UFUNCTION ( )
	void NotifyChunkUnload ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex ) const;

	UFUNCTION ( )
	void NotifyChunkUpdate ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex , const FLPPAsyncChunkManagerAction& ActionData ) const;

protected:

	UFUNCTION ( )
	AActor* AllocateChunkActor ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex );

protected:

	UPROPERTY ( Transient )
	TMap < FIntVector , FLPPLoadedChunkData > LoadedChunkMap;

	UPROPERTY ( Transient )
	TArray < TObjectPtr < AActor > > AvailableChunkList;

	UPROPERTY ( Transient )
	TArray < TObjectPtr < ULFPChunkedTagDataComponent > > DataComponentList;

	UPROPERTY ( Transient )
	TArray < TObjectPtr < ULFPChunkedGridPositionComponent > > PositionComponentList;

	UPROPERTY ( Transient )
	TArray < float > ComponentHeightList;

	UPROPERTY ( Transient )
	TArray < FVector > ComponentChunkGapList;

protected:

	UPROPERTY ( Transient )
	float TickBudget = 0.0f;

	UPROPERTY ( Transient )
	TSubclassOf < AActor > ChunkActorClass = nullptr;

	UPROPERTY ( Transient )
	FVector SpawnOffset = FVector ( 0.0f , 0.0f , 0.0f );

private:

	UPROPERTY ( Transient )
	TArray < FIntVector > AsyncLoadChunk = TArray < FIntVector > ( );

	UPROPERTY ( Transient )
	TMap < FIntVector , FLPPAsyncChunkManagerAction > BatchUpdateList = TMap < FIntVector , FLPPAsyncChunkManagerAction > ( );
};
