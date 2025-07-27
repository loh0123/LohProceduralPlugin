// Copyright (c) 2025 Loh Zhi Kang ( loh0123@hotmail.com )
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Library/LPPGridDataLibrary.h"
#include "LPPChunkManager.generated.h"


class ULFPChunkedTagDataComponent;
class ULFPChunkedIndexTranslator;
class ULPPChunkController;

UCLASS ( ClassGroup=(Custom) , meta=(BlueprintSpawnableComponent) )
class LOHPROCEDURALPLUGIN_API ULPPChunkManager : public UActorComponent
{
	GENERATED_BODY ( )

public:

	// Sets default values for this component's properties
	ULPPChunkManager ( );

protected:

	virtual void OnRegister ( ) override;

	// Called when the game starts
	virtual void BeginPlay ( ) override;

	virtual void EndPlay ( const EEndPlayReason::Type EndPlayReason ) override;

public:

	// Called every frame
	virtual void TickComponent ( float DeltaTime , ELevelTick TickType , FActorComponentTickFunction* ThisTickFunction ) override;

public:

	UFUNCTION ( BlueprintPure , Category = "Default" )
	ULFPChunkedTagDataComponent* GetDataComponent ( ) const;

	UFUNCTION ( BlueprintPure , Category = "Default" )
	ULFPChunkedIndexTranslator* GetIndexTranslator ( ) const;

public:

	UFUNCTION ( BlueprintCallable , Category = "Default" )
	void LoadChunkIndex ( const int32 RegionIndex , const int32 ChunkIndex , const bool bLoadNearby , const bool bLoadVisitList );

protected:

	UFUNCTION ( )
	void OnDataTagChanged ( const int32 RegionIndex , const int32 ChunkIndex , const int32 DataIndex , const FGameplayTag& OldTag , const FGameplayTag& NewTag );

	UFUNCTION ( )
	void OnChunkInitialized ( const int32 RegionIndex , const int32 ChunkIndex );

	UFUNCTION ( )
	void OnChunkUninitialized ( const int32 RegionIndex , const int32 ChunkIndex );

protected:

	UFUNCTION ( )
	void ReserveChunkActor ( );

	UFUNCTION ( )
	void MarkChunkUpdate ( const int32 RegionIndex , const int32 ChunkIndex );

protected:

	UFUNCTION ( )
	void LoadChunkByNearbyPoint ( );

	UFUNCTION ( )
	void LoadChunkByVisitList ( );

	UFUNCTION ( )
	void UnloadChunkByDistance ( );

	UFUNCTION ( )
	void UnloadChunkByVisitList ( );

protected:

	UFUNCTION ( )
	void IterateVisitableChunkList ( );

	UFUNCTION ( )
	bool IsChunkVisibleToPlayer ( const int32 RegionIndex , const int32 ChunkIndex ) const;

protected:

	UPROPERTY ( Transient )
	TMap < FIntPoint , int32 > VisitedChunkList;

	UPROPERTY ( Transient )
	TArray < FLPP_VisitableChunkData > NextVisitableChunkList;

	UPROPERTY ( Transient )
	FIntPoint CurrentCenterChunkIndex = FIntPoint::NoneValue;

protected:

	UPROPERTY ( Transient )
	TSet < FIntPoint > ChunkUpdateList = TSet < FIntPoint > ( );

	UPROPERTY ( Transient )
	TMap < FIntPoint , TObjectPtr < ULPPChunkController > > LoadedChunkMap = TMap < FIntPoint , TObjectPtr < ULPPChunkController > > ( );

	UPROPERTY ( Transient )
	TArray < TObjectPtr < ULPPChunkController > > AvailableChunkList = TArray < TObjectPtr < ULPPChunkController > > ( );

	UPROPERTY ( Transient )
	TObjectPtr < ULFPChunkedTagDataComponent > DataComponent = nullptr;

protected:

	UPROPERTY ( EditAnywhere , Category = "Setting|ChunkData" )
	FGameplayTag ConnectionBlockTag = FGameplayTag ( );

	UPROPERTY ( EditAnywhere , Category = "Setting|ChunkData" )
	FGameplayTag ConnectionMetaTag = FGameplayTag ( );

protected:

	UPROPERTY ( EditAnywhere , Category = "Setting" )
	TSubclassOf < AActor > ChunkActorClass = nullptr;

	UPROPERTY ( EditAnywhere , Category = "Setting" )
	TObjectPtr < ULFPChunkedIndexTranslator > ChunkIndexTranslator = nullptr;

	UPROPERTY ( EditAnywhere , Category = "Setting" )
	uint8 AsyncIterateAmount = 4;

	UPROPERTY ( EditAnywhere , Category = "Setting" )
	uint8 VisibleWeight = 2;

	UPROPERTY ( EditAnywhere , Category = "Setting" )
	int32 NearbyDistance = 1;

protected:

	UPROPERTY ( EditAnywhere )
	uint8 MaxLODDistance = 8;
};
