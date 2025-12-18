// Copyright (c) 2025 Loh Zhi Kang ( loh0123@hotmail.com )
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)


#include "Components/LPPChunkRequester.h"

#include "Components/LFPChunkedGridPositionComponent.h"
#include "Components/LFPChunkedTagDataComponent.h"
#include "Subsystem/LPPChunkManagerSubsystem.h"


// Sets default values for this component's properties
ULPPChunkRequester::ULPPChunkRequester ( )
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;

	// ...
}


// Called when the game starts
void ULPPChunkRequester::BeginPlay ( )
{
	Super::BeginPlay ( );

	// ...
}


// Called every frame
void ULPPChunkRequester::TickComponent ( float DeltaTime , ELevelTick TickType , FActorComponentTickFunction* ThisTickFunction )
{
	Super::TickComponent ( DeltaTime , TickType , ThisTickFunction );

	// ...
}

void ULPPChunkRequester::ChangeLoadChunkIndex ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex )
{
	const FIntVector NewCenterChunkIndex ( ComponentIndex , RegionIndex , ChunkIndex );

	if ( CurrentCenterChunkIndex == NewCenterChunkIndex )
	{
		return;
	}

	CurrentCenterChunkIndex = NewCenterChunkIndex;

	UnloadOutBoundChunk ( );
	LoadChunkByNearbyPoint ( );
}

void ULPPChunkRequester::LoadChunkByNearbyPoint ( )
{
	if ( IsValid ( GetWorld ( ) ) == false )
	{
		return;
	}

	ULPPChunkManagerSubsystem* ManagerSystem;

	if ( ManagerSystem = GetWorld ( )->GetSubsystem < ULPPChunkManagerSubsystem > ( ) ; IsValid ( ManagerSystem ) == false )
	{
		return;
	}

	const ULFPChunkedTagDataComponent*      DataComponent     = ManagerSystem->GetDataComponent ( CurrentCenterChunkIndex.X );
	const ULFPChunkedGridPositionComponent* PositionComponent = ManagerSystem->GetPositionComponent ( CurrentCenterChunkIndex.X );

	if ( IsValid ( DataComponent ) == false )
	{
		return;
	}

	if ( IsValid ( PositionComponent ) == false )
	{
		return;
	}

	for ( int32 Index_X = -NearbyLoadDistance ; Index_X <= NearbyLoadDistance ; ++Index_X )
	{
		for ( int32 Index_Y = -NearbyLoadDistance ; Index_Y <= NearbyLoadDistance ; ++Index_Y )
		{
			for ( int32 Index_Z = -NearbyLoadDistance ; Index_Z <= NearbyLoadDistance ; ++Index_Z )
			{
				const FIntVector LoadOffset ( Index_X , Index_Y , Index_Z );
				const FIntPoint  ChunkGridIndex = PositionComponent->AddOffsetToChunkGridIndex ( FIntPoint ( CurrentCenterChunkIndex.Y , CurrentCenterChunkIndex.Z ) , LoadOffset );
				const FIntVector LoadChunkIndex ( CurrentCenterChunkIndex.X , ChunkGridIndex.X , ChunkGridIndex.Y );

				if ( LoadedChunkMap.Contains ( LoadChunkIndex ) == false && DataComponent->IsChunkIndexValid ( ChunkGridIndex.X , ChunkGridIndex.Y ) )
				{
					LoadedChunkMap.Add ( LoadOffset );

					ManagerSystem->LoadChunk ( LoadChunkIndex.X , LoadChunkIndex.Y , LoadChunkIndex.Z , GetOwner ( ) );
				}
			}
		}
	}
}

void ULPPChunkRequester::UnloadOutBoundChunk ( )
{
	if ( IsValid ( GetWorld ( ) ) == false )
	{
		return;
	}

	ULPPChunkManagerSubsystem* ManagerSystem;

	if ( ManagerSystem = GetWorld ( )->GetSubsystem < ULPPChunkManagerSubsystem > ( ) ; IsValid ( ManagerSystem ) == false )
	{
		return;
	}

	const ULFPChunkedTagDataComponent*      DataComponent     = ManagerSystem->GetDataComponent ( CurrentCenterChunkIndex.X );
	const ULFPChunkedGridPositionComponent* PositionComponent = ManagerSystem->GetPositionComponent ( CurrentCenterChunkIndex.X );

	if ( IsValid ( DataComponent ) == false )
	{
		return;
	}

	if ( IsValid ( PositionComponent ) == false )
	{
		return;
	}

	if ( DataComponent->IsChunkIndexValid ( CurrentCenterChunkIndex.Y , CurrentCenterChunkIndex.Z ) == false )
	{
		return;
	}

	TArray < FIntVector > RemoveList;

	for ( const FIntVector& LoadedChunkIndex : LoadedChunkMap )
	{
		if ( LoadedChunkIndex.X != CurrentCenterChunkIndex.X )
		{
			RemoveList.Add ( LoadedChunkIndex );
		}
		else
		{
			const FIntVector Distance = PositionComponent->GetDistanceToChunkGridIndex ( FIntPoint ( LoadedChunkIndex.Y , LoadedChunkIndex.Z ) , FIntPoint ( CurrentCenterChunkIndex.Y , CurrentCenterChunkIndex.Z ) );

			if ( Distance.GetMax ( ) > MaxLoadDistance )
			{
				RemoveList.Add ( LoadedChunkIndex );
			}
		}
	}

	for ( const FIntVector& RemoveIndex : RemoveList )
	{
		LoadedChunkMap.Remove ( RemoveIndex );

		ManagerSystem->UnloadChunk ( RemoveIndex.X , RemoveIndex.Y , RemoveIndex.Z , GetOwner ( ) );
	}
}
