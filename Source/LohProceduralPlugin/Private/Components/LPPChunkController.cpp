// Copyright (c) 2025 Loh Zhi Kang ( loh0123@hotmail.com )
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)


#include "Components/LPPChunkController.h"

#include "Components/LFPGridTagDataComponent.h"
#include "Components/LPPChunkManager.h"


// Sets default values for this component's properties
ULPPChunkController::ULPPChunkController ( )
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;

	// ...
}

void ULPPChunkController::OnRegister ( )
{
	Super::OnRegister ( );

	OwingManager = GetOwner ( )->FindComponentByClass < ULPPChunkManager > ( );

	// Find It On Actor Owner
	if ( IsValid ( OwingManager ) == false && IsValid ( GetOwner ( )->GetOwner ( ) ) )
	{
		OwingManager = GetOwner ( )->GetOwner ( )->FindComponentByClass < ULPPChunkManager > ( );
	}
}


// Called when the game starts
void ULPPChunkController::BeginPlay ( )
{
	Super::BeginPlay ( );

	// ...
}


// Called every frame
void ULPPChunkController::TickComponent ( float DeltaTime , ELevelTick TickType , FActorComponentTickFunction* ThisTickFunction )
{
	Super::TickComponent ( DeltaTime , TickType , ThisTickFunction );

	// ...
}

void ULPPChunkController::UpdateChunk ( )
{
	OnChunkDataChange.Broadcast ( );
}

void ULPPChunkController::SetLODIndex ( const int32 NewLODIndex )
{
	if ( CurrentLODIndex != NewLODIndex )
	{
		CurrentLODIndex = NewLODIndex < 0 || NewLODIndex > 2 ? INDEX_NONE : NewLODIndex;

		OnChunkLODChanged.Broadcast ( CurrentLODIndex );
	}
}

void ULPPChunkController::ClearLODIndex ( )
{
	CurrentLODIndex = INDEX_NONE;

	OnChunkLODChanged.Broadcast ( CurrentLODIndex );
}

void ULPPChunkController::SetChunkIndex ( const int32 NewRegionIndex , const int32 NewChunkIndex )
{
	if ( IsValid ( OwingManager ) == false || IsValid ( OwingManager->GetDataComponent ( ) ) == false )
	{
		return;
	}

	if ( RegionIndex == NewRegionIndex && ChunkIndex == NewChunkIndex )
	{
		return;
	}

	RegionIndex = NewRegionIndex;
	ChunkIndex  = NewChunkIndex;

	OnChunkIndexChange.Broadcast ( RegionIndex , ChunkIndex , OwingManager->GetDataComponent ( )->ToChunkGridPosition ( FIntPoint ( RegionIndex , ChunkIndex ) ) );
}

ULPPChunkManager* ULPPChunkController::GetOwingManager ( ) const
{
	return OwingManager;
}

int32 ULPPChunkController::GetRegionIndex ( ) const
{
	return RegionIndex;
}

int32 ULPPChunkController::GetChunkIndex ( ) const
{
	return ChunkIndex;
}

int32 ULPPChunkController::GetLODIndex ( ) const
{
	return CurrentLODIndex;
}

bool ULPPChunkController::IsChunkVisible ( ) const
{
	return CurrentLODIndex != INDEX_NONE;
}
