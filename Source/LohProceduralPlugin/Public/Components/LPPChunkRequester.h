// Copyright (c) 2025 Loh Zhi Kang ( loh0123@hotmail.com )
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Components/ActorComponent.h"
#include "Library/LPPGridDataLibrary.h"
#include "LPPChunkRequester.generated.h"


/*
 * Chunk Requester
 * - 
 */
UCLASS ( ClassGroup=(Custom) , meta=(BlueprintSpawnableComponent) )
class LOHPROCEDURALPLUGIN_API ULPPChunkRequester : public UActorComponent
{
	GENERATED_BODY ( )

public:

	// Sets default values for this component's properties
	ULPPChunkRequester ( );

protected:

	// Called when the game starts
	virtual void BeginPlay ( ) override;

public:

	// Called every frame
	virtual void TickComponent ( float DeltaTime , ELevelTick TickType , FActorComponentTickFunction* ThisTickFunction ) override;

protected:

	UFUNCTION ( BlueprintCallable , Category = "Default" )
	void ChangeLoadChunkIndex ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex );

	//UFUNCTION ( )
	//void MarkChunkUpdate ( const int32 RegionIndex , const int32 ChunkIndex );

protected:

	UFUNCTION ( )
	void LoadChunkByNearbyPoint ( );

	//UFUNCTION ( )
	//void LoadChunkByVisitList ( );
	//
	//UFUNCTION ( )
	//void UnloadChunkByDistance ( );
	//
	//UFUNCTION ( )
	//void UnloadChunkByVisitList ( );
	//
	//UFUNCTION ( )
	//void UnloadChunkList ( );

	UFUNCTION ( )
	void UnloadOutBoundChunk ( );

protected:

	//UFUNCTION ( )
	//void IterateVisitableChunkList ( );
	//
	//UFUNCTION ( )
	//bool IsChunkVisibleToPlayer ( const int32 RegionIndex , const int32 ChunkIndex ) const;

protected:

	//UPROPERTY ( Transient )
	//TMap < FIntPoint , int32 > VisitedChunkList;
	//
	//UPROPERTY ( Transient )
	//TArray < FLPP_VisitableChunkData > NextVisitableChunkList;
	//
	UPROPERTY ( Transient )
	FIntVector CurrentCenterChunkIndex = FIntVector::NoneValue;

protected:

	//UPROPERTY ( Transient )
	//TSet < FIntPoint > ChunkUpdateList = TSet < FIntPoint > ( );

	UPROPERTY ( Transient )
	TSet < FIntVector > LoadedChunkMap = TSet < FIntVector > ( );

protected:

	//UPROPERTY ( EditAnywhere , Category = "Setting|ChunkData" )
	//FGameplayTag ConnectionBlockTag = FGameplayTag ( );
	//
	//UPROPERTY ( EditAnywhere , Category = "Setting|ChunkData" )
	//FGameplayTag ConnectionMetaTag = FGameplayTag ( );

protected:

	//UPROPERTY ( EditAnywhere , Category = "Setting" )
	//TSubclassOf < AActor > ChunkActorClass = nullptr;
	//
	//UPROPERTY ( EditAnywhere , Category = "Setting" )
	//uint8 AsyncIterateAmount = 4;
	//
	//UPROPERTY ( EditAnywhere , Category = "Setting" )
	//uint8 VisibleWeight = 2;
	//
	//UPROPERTY ( EditAnywhere , Category = "Setting" )
	//int32 NearbyDistance = 1;

protected:

	UPROPERTY ( EditAnywhere , Category = "Setting" )
	uint8 NearbyLoadDistance = 2;

	UPROPERTY ( EditAnywhere , Category = "Setting" )
	uint8 MaxLoadDistance = 2;

	UPROPERTY ( EditAnywhere , Category = "Setting" )
	bool bIsolatedRegion = false;
};
