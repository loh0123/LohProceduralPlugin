// Copyright (c) 2025 Loh Zhi Kang ( loh0123@hotmail.com )
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)


#include "Subsystem/LPPChunkManagerSubsystem.h"

#include "Components/LFPGridTagDataComponent.h"
#include "Interface/LPPChunkActorInterface.h"
#include "Math/LFPGridLibrary.h"

void ULPPChunkManagerSubsystem::Initialize ( FSubsystemCollectionBase& Collection )
{
	Super::Initialize ( Collection );
}

void ULPPChunkManagerSubsystem::Tick ( float DeltaTime )
{
	for ( int32 LoadIndex = 0 ; LoadIndex < 8 && AsyncLoadChunk.IsEmpty ( ) == false ; ++LoadIndex )
	{
		const FIntVector ChunkIndex = AsyncLoadChunk.Pop ( EAllowShrinking::No );

		NotifyChunkLoad ( ChunkIndex.X , ChunkIndex.Y , ChunkIndex.Z );
	}

	for ( auto& ActionData : BatchUpdateList )
	{
		NotifyChunkUpdate ( ActionData.Key.X , ActionData.Key.Y , ActionData.Key.Z , ActionData.Value.UpdateDataIndexList.Array ( ) );
	}

	BatchUpdateList.Reset ( );
}

TStatId ULPPChunkManagerSubsystem::GetStatId ( ) const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT ( ULPPChunkManagerSubsystem , STATGROUP_Tickables );
}

void ULPPChunkManagerSubsystem::SetupChunkManager ( const TArray < ULFPGridTagDataComponent* >& NewDataComponentList , const TSubclassOf < AActor > NewChunkActorClass , const FVector& NewSpawnOffset , const FVector& ChunkDataSize )
{
	DataComponentList = NewDataComponentList;

	ChunkActorClass = NewChunkActorClass;
	SpawnOffset     = NewSpawnOffset;

	{
		for ( AActor* AvailableChunkActor : AvailableChunkList )
		{
			if ( IsValid ( AvailableChunkActor ) )
			{
				AvailableChunkActor->Destroy ( );
			}
		}

		AvailableChunkList.Reset ( );
	}

	{
		for ( auto& LoadedChunk : LoadedChunkMap )
		{
			if ( IsValid ( LoadedChunk.Value.ChunkActor ) )
			{
				LoadedChunk.Value.ChunkActor->Destroy ( );
			}
		}

		LoadedChunkMap.Reset ( );
	}

	{
		ComponentChunkGapList.Reset ( );

		for ( int32 LoopComponentIndex = 0 ; LoopComponentIndex < DataComponentList.Num ( ) ; ++LoopComponentIndex )
		{
			ComponentChunkGapList.Add ( static_cast < FVector > ( DataComponentList [ LoopComponentIndex ]->GetDataGridSize ( ) ) * ChunkDataSize );
		}
	}

	{
		ComponentHeightList.Reset ( );

		float ComponentHeight = 0.0f;

		for ( int32 LoopComponentIndex = 0 ; LoopComponentIndex < DataComponentList.Num ( ) ; ++LoopComponentIndex )
		{
			ComponentHeight += DataComponentList [ LoopComponentIndex ]->GetChunkGridSize ( ).Z * ComponentChunkGapList [ LoopComponentIndex ].Z;

			ComponentHeightList.Add ( ComponentHeight );
		}
	}
}

FVector ULPPChunkManagerSubsystem::GetChunkLocation ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex ) const
{
	if ( DataComponentList.IsValidIndex ( ComponentIndex ) == false )
	{
		return FVector::ZeroVector;
	}

	FVector ChunkLocation = FVector ( DataComponentList [ ComponentIndex ]->ToChunkGridPosition ( FIntPoint ( RegionIndex , ChunkIndex ) ) ) * ComponentChunkGapList [ ComponentIndex ];

	ChunkLocation.Z += ComponentHeightList [ ComponentIndex ];
	ChunkLocation += SpawnOffset;

	return ChunkLocation;
}

ULFPGridTagDataComponent* ULPPChunkManagerSubsystem::GetDataComponent ( const int32 ComponentIndex ) const
{
	if ( DataComponentList.IsValidIndex ( ComponentIndex ) == false )
	{
		return nullptr;
	}

	return DataComponentList [ ComponentIndex ];
}

AActor* ULPPChunkManagerSubsystem::LoadChunk ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex , AActor* LoaderActor )
{
	if ( IsValid ( GetWorld ( ) ) == false )
	{
		return nullptr;
	}

	if ( IsValid ( LoaderActor ) == false )
	{
		return nullptr;
	}

	if ( IsValid ( ChunkActorClass ) == false )
	{
		return nullptr;
	}

	if ( DataComponentList.IsValidIndex ( ComponentIndex ) == false )
	{
		return nullptr;
	}

	FLPPLoadedChunkData& LoadedChunkRef = LoadedChunkMap.FindOrAdd ( FIntVector ( ComponentIndex , RegionIndex , ChunkIndex ) );

	// Check do we already spawn the chunk actor
	if ( IsValid ( LoadedChunkRef.ChunkActor ) == false )
	{
		LoadedChunkRef.ChunkActor = AllocateChunkActor ( ComponentIndex , RegionIndex , ChunkIndex );
	}

	// Do we have a chunk actor?
	if ( IsValid ( LoadedChunkRef.ChunkActor ) )
	{
		// Are we calling this function too much?
		if ( LoadedChunkRef.LoaderList.Contains ( LoaderActor ) )
		{
			UE_LOG ( LogTemp , Error , TEXT ( "LoaderActor ( %s ) already in LoaderList" ) , *LoaderActor->GetName () );
		}
		else
		{
			LoadedChunkRef.LoaderList.Add ( LoaderActor );

			if ( LoadedChunkRef.LoaderList.Num ( ) == 1 )
			{
				AsyncLoadChunk.Add ( FIntVector ( ComponentIndex , RegionIndex , ChunkIndex ) );
			}
		}

		//NotifyChunkLoad ( ComponentIndex , RegionIndex , ChunkIndex );

		return LoadedChunkRef.ChunkActor;
	}


	return nullptr;
}

bool ULPPChunkManagerSubsystem::UnloadChunk ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex , AActor* LoaderActor )
{
	if ( IsValid ( GetWorld ( ) ) == false )
	{
		return false;
	}

	if ( IsValid ( LoaderActor ) == false )
	{
		return false;
	}

	const FIntVector ChunkID ( ComponentIndex , RegionIndex , ChunkIndex );

	// Are we loaded a chunk actor?
	if ( FLPPLoadedChunkData* LoadedChunkPtr = LoadedChunkMap.Find ( ChunkID ) ; LoadedChunkPtr != nullptr )
	{
		// Is LoaderActor are in the list and removed?
		const int32 RemoveCount = LoadedChunkPtr->LoaderList.RemoveSwap ( LoaderActor );

		// List is empty and prepare to be removed
		if ( LoadedChunkPtr->LoaderList.IsEmpty ( ) )
		{
			if ( IsValid ( LoadedChunkPtr->ChunkActor ) )
			{
				// This actor can be reuse
				AvailableChunkList.Add ( LoadedChunkPtr->ChunkActor );

				const int32 LoadListIndex = AsyncLoadChunk.IndexOfByKey ( FIntVector ( ComponentIndex , RegionIndex , ChunkIndex ) );

				if ( LoadListIndex != INDEX_NONE )
				{
					AsyncLoadChunk.RemoveAt ( LoadListIndex );
				}
				else
				{
					NotifyChunkUnload ( LoadedChunkPtr->ChunkActor );
				}
			}

			LoadedChunkMap.Remove ( ChunkID );
		}

		return RemoveCount > 0;
	}

	return false;
}

void ULPPChunkManagerSubsystem::RequestChunkUpdate ( const int32 ComponentIndex , const TArray < FIntVector >& GridDataIndexList , const bool bIsolateRegion )
{
	if ( ComponentIndex <= INDEX_NONE || IsValid ( DataComponentList [ ComponentIndex ] ) == false )
	{
		return;
	}

	TMap < FIntPoint , TSet < int32 > > BroadcastChunkIDList;

	for ( const FIntVector& GridDataIndex : GridDataIndexList )
	{
		BroadcastChunkIDList.FindOrAdd ( FIntPoint ( GridDataIndex.X , GridDataIndex.Y ) ).Add ( GridDataIndex.Z );

		const FIntVector DataPos = DataComponentList [ ComponentIndex ]->ToDataGridPosition ( FIntVector ( 0 , 0 , GridDataIndex.Z ) );

		// Send update to any nearby chunk too if loaded
		for ( const FIntVector& GridEdgeDir : ULFPGridLibrary::GetGridEdgeDirection ( DataPos , DataComponentList [ ComponentIndex ]->GetDataGridSize ( ) ) )
		{
			const FIntPoint EdgeChunkIndex = DataComponentList [ ComponentIndex ]->AddOffsetToChunkGridIndex ( FIntPoint ( GridDataIndex.X , GridDataIndex.Y ) , GridEdgeDir );

			if ( bIsolateRegion && EdgeChunkIndex.X != GridDataIndex.X )
			{
				continue;
			}

			BroadcastChunkIDList.FindOrAdd ( EdgeChunkIndex );
		}
	}
	for ( const TPair < FIntPoint , TSet < int32 > >& ChunkID : BroadcastChunkIDList )
	{
		auto& ActionData = BatchUpdateList.FindOrAdd ( FIntVector ( ComponentIndex , ChunkID.Key.X , ChunkID.Key.Y ) );

		ActionData.UpdateDataIndexList.Append ( ChunkID.Value );
	}
}

void ULPPChunkManagerSubsystem::NotifyChunkLoad ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex ) const
{
	const FLPPLoadedChunkData* LoadedChunkRef = LoadedChunkMap.Find ( FIntVector ( ComponentIndex , RegionIndex , ChunkIndex ) );

	// Do we have a chunk actor?
	if ( LoadedChunkRef != nullptr && IsValid ( LoadedChunkRef->ChunkActor ) )
	{
		// Does the chunk actor we spawn have the correct interface?
		if ( LoadedChunkRef->ChunkActor->Implements < ULPPChunkActorInterface > ( ) )
		{
			ILPPChunkActorInterface::Execute_OnChunkIDChanged ( LoadedChunkRef->ChunkActor , DataComponentList [ ComponentIndex ] , RegionIndex , ChunkIndex );
		}
		else
		{
			UE_LOG ( LogTemp , Error , TEXT ( "ChunkActor ( %s) does not implement ULPPChunkActorInterface" ) , *LoadedChunkRef->ChunkActor->GetName () );
		}
	}
}

void ULPPChunkManagerSubsystem::NotifyChunkUnload ( AActor* UnloadActor ) const
{
	if ( IsValid ( UnloadActor ) )
	{
		// Does the chunk actor we spawn have the correct interface?
		if ( UnloadActor->Implements < ULPPChunkActorInterface > ( ) )
		{
			// Tell the actor the id is invalid now 
			ILPPChunkActorInterface::Execute_OnChunkIDChanged ( UnloadActor , nullptr , INDEX_NONE , INDEX_NONE );
		}
		else
		{
			UE_LOG ( LogTemp , Error , TEXT ( "ChunkActor ( %s) does not implement ULPPChunkActorInterface" ) , *UnloadActor->GetName () );
		}
	}
}

void ULPPChunkManagerSubsystem::NotifyChunkUpdate ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex , const TArray < int32 >& DataIndexList ) const
{
	const FIntVector ChunkID ( ComponentIndex , RegionIndex , ChunkIndex );

	if ( const FLPPLoadedChunkData* ChunkRef = LoadedChunkMap.Find ( ChunkID ) ; ChunkRef != nullptr && IsValid ( ChunkRef->ChunkActor ) )
	{
		// Does the chunk actor we spawn have the correct interface?
		if ( ChunkRef->ChunkActor->Implements < ULPPChunkActorInterface > ( ) )
		{
			ILPPChunkActorInterface::Execute_OnRequestChunkUpdate ( ChunkRef->ChunkActor , DataIndexList );
		}
		else
		{
			UE_LOG ( LogTemp , Error , TEXT ( "ChunkActor ( %s) does not implement ULPPChunkActorInterface" ) , *ChunkRef->ChunkActor->GetName () );
		}
	}
}

AActor* ULPPChunkManagerSubsystem::AllocateChunkActor ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex )
{
	if ( IsValid ( GetWorld ( ) ) == false )
	{
		return nullptr;
	}

	if ( IsValid ( ChunkActorClass ) == false )
	{
		return nullptr;
	}

	if ( DataComponentList.IsValidIndex ( ComponentIndex ) == false )
	{
		return nullptr;
	}

	if ( RegionIndex < 0 || ChunkIndex < 0 )
	{
		return nullptr;
	}

	AActor* NewChunkActor = nullptr;

	if ( AvailableChunkList.IsEmpty ( ) )
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		const FTransform SpawnTransform ( GetChunkLocation ( ComponentIndex , RegionIndex , ChunkIndex ) );

		NewChunkActor = GetWorld ( )->SpawnActor ( ChunkActorClass , &SpawnTransform , SpawnParameters );
	}
	else
	{
		AvailableChunkList.RemoveAllSwap ( [] ( const AActor* AvailableChunkActor ) { return IsValid ( AvailableChunkActor ) == false; } );

		if ( AvailableChunkList.IsEmpty ( ) == false )
		{
			NewChunkActor = AvailableChunkList.Pop ( );

			NewChunkActor->SetActorLocation ( GetChunkLocation ( ComponentIndex , RegionIndex , ChunkIndex ) );
		}
	}

	return NewChunkActor;
}
