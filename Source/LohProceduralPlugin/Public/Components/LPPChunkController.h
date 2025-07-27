// Copyright (c) 2025 Loh Zhi Kang ( loh0123@hotmail.com )
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LPPChunkController.generated.h"


class ULPPChunkManager;
DECLARE_DYNAMIC_MULTICAST_DELEGATE ( FLPPChunkController_DataChange );

PRAGMA_DISABLE_DEPRECATION_WARNINGS // TODO : Remove this on next engine version ( Newer than 5.6 )
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams ( FLPPChunkController_IndexChange , const int32 , RegionIndex , const int32 , ChunkIndex , const FIntVector , GirdLocation );

PRAGMA_ENABLE_DEPRECATION_WARNINGS

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam ( FLPPChunkController_LODChange , const int32 , LODIndex );

UCLASS ( ClassGroup=(Custom) , meta=(BlueprintSpawnableComponent) )
class LOHPROCEDURALPLUGIN_API ULPPChunkController : public UActorComponent
{
	GENERATED_BODY ( )

public:

	// Sets default values for this component's properties
	ULPPChunkController ( );

protected:

	// Called when the actor spawn and register the component
	virtual void OnRegister ( ) override;

	// Called when the game starts
	virtual void BeginPlay ( ) override;

public:

	// Called every frame
	virtual void TickComponent ( float DeltaTime , ELevelTick TickType , FActorComponentTickFunction* ThisTickFunction ) override;

public:

	UFUNCTION ( BlueprintCallable , Category = "Default" )
	void UpdateChunk ( );

	UFUNCTION ( BlueprintCallable , Category = "Default" )
	void SetLODIndex ( const int32 NewLODIndex );

	UFUNCTION ( BlueprintCallable , Category = "Default" )
	void ClearLODIndex ( );

	UFUNCTION ( BlueprintCallable , Category = "Default" )
	void SetChunkIndex ( const int32 NewRegionIndex , const int32 NewChunkIndex );

public:

	UFUNCTION ( BlueprintPure , Category = "Default" )
	ULPPChunkManager* GetOwingManager ( ) const;

public:

	UFUNCTION ( BlueprintPure , Category = "Default" )
	int32 GetRegionIndex ( ) const;

	UFUNCTION ( BlueprintPure , Category = "Default" )
	int32 GetChunkIndex ( ) const;

	UFUNCTION ( BlueprintPure , Category = "Default" )
	int32 GetLODIndex ( ) const;

	UFUNCTION ( BlueprintPure , Category = "Default" )
	bool IsChunkVisible ( ) const;

public:

	UPROPERTY ( BlueprintAssignable , Category="Default" )
	FLPPChunkController_DataChange OnChunkDataChange;

	UPROPERTY ( BlueprintAssignable , Category="Default" )
	FLPPChunkController_IndexChange OnChunkIndexChange;

	UPROPERTY ( BlueprintAssignable , Category="Default" )
	FLPPChunkController_LODChange OnChunkLODChanged;

protected:

	UPROPERTY ( Transient )
	TObjectPtr < ULPPChunkManager > OwingManager = nullptr;

	UPROPERTY ( Transient )
	int32 RegionIndex = INDEX_NONE;

	UPROPERTY ( Transient )
	int32 ChunkIndex = INDEX_NONE;

	UPROPERTY ( Transient )
	int32 CurrentLODIndex = INDEX_NONE;
};
