// Copyright (c) 2025 Loh Zhi Kang ( loh0123@hotmail.com )
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)


#include "Components/LPPChunkManager.h"

#include "Components/LFPChunkedTagDataComponent.h"
#include  "Data/LFPChunkedIndexTranslator.h"
#include "Components/LPPChunkController.h"
#include "Math/LFPGridLibrary.h"


// Sets default values for this component's properties
ULPPChunkManager::ULPPChunkManager ( )
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}

void ULPPChunkManager::OnRegister ( )
{
	Super::OnRegister ( );

	DataComponent = GetOwner ( )->FindComponentByClass < ULFPChunkedTagDataComponent > ( );
}

// Called when the game starts
void ULPPChunkManager::BeginPlay ( )
{
	Super::BeginPlay ( );

	if ( IsValid ( DataComponent ) )
	{
		DataComponent->OnTagChanged.AddDynamic ( this , &ULPPChunkManager::OnDataTagChanged );
		DataComponent->OnInitialized.AddDynamic ( this , &ULPPChunkManager::OnChunkInitialized );
		DataComponent->OnUninitialized.AddDynamic ( this , &ULPPChunkManager::OnChunkUninitialized );
	}

	ReserveChunkActor ( );
}

void ULPPChunkManager::EndPlay ( const EEndPlayReason::Type EndPlayReason )
{
	Super::EndPlay ( EndPlayReason );

	LoadedChunkMap.Empty ( );
	AvailableChunkList.Empty ( );

	if ( IsValid ( DataComponent ) )
	{
		DataComponent->OnTagChanged.RemoveAll ( this );
		DataComponent->OnInitialized.RemoveAll ( this );
		DataComponent->OnUninitialized.RemoveAll ( this );

		DataComponent = nullptr;
	}
}


// Called every frame
void ULPPChunkManager::TickComponent ( float DeltaTime , ELevelTick TickType , FActorComponentTickFunction* ThisTickFunction )
{
	Super::TickComponent ( DeltaTime , TickType , ThisTickFunction );

	if ( ChunkUpdateList.IsEmpty ( ) == false )
	{
		for ( const FIntPoint& ChunkUpdateIndex : ChunkUpdateList )
		{
			ULPPGridDataLibrary::SetChunkConnectionMetaData ( DataComponent , ChunkIndexTranslator , ChunkUpdateIndex.X , ChunkUpdateIndex.Y , ConnectionBlockTag , ConnectionMetaTag , false );
		}

		ChunkUpdateList.Reset ( );

		LoadChunkByVisitList ( );
	}
	else
	{
		for ( uint8 IterateIndex = 0 ; IterateIndex < AsyncIterateAmount && NextVisitableChunkList.IsEmpty ( ) == false ; ++IterateIndex )
		{
			// Commit Load Chunk
			for ( const auto& VisitedChunk : NextVisitableChunkList )
			{
				const FIntPoint CurrentChunkIndex = FIntPoint ( VisitedChunk.RegionIndex , VisitedChunk.ChunkIndex );
				const int32     DistanceToPlayer  = VisitedChunk.Step;
				const int32     TargetLODIndex    = DistanceToPlayer <=
				                             ( MaxLODDistance >> 2 )
				                             ? 0
				                             : DistanceToPlayer <= ( MaxLODDistance >> 1 )
				                             ? 1
				                             : 2;

				if ( LoadedChunkMap.Contains ( CurrentChunkIndex ) )
				{
					LoadedChunkMap.FindChecked ( CurrentChunkIndex )->SetLODIndex ( TargetLODIndex );
				}
				else if ( AvailableChunkList.IsEmpty ( ) == false )
				{
					if ( ULPPChunkController* TargetComponent = AvailableChunkList.Pop ( EAllowShrinking::No ) ; IsValid ( TargetComponent ) )
					{
						TargetComponent->SetChunkIndex ( CurrentChunkIndex.X , CurrentChunkIndex.Y );
						TargetComponent->SetLODIndex ( TargetLODIndex );

						LoadedChunkMap.Add ( CurrentChunkIndex , TargetComponent );
					}
				}
			}

			IterateVisitableChunkList ( );
		}
	}
}

ULFPChunkedTagDataComponent* ULPPChunkManager::GetDataComponent ( ) const
{
	return DataComponent;
}

ULFPChunkedIndexTranslator* ULPPChunkManager::GetIndexTranslator ( ) const
{
	return ChunkIndexTranslator;
}

void ULPPChunkManager::LoadChunkIndex ( const int32 RegionIndex , const int32 ChunkIndex , const bool bLoadNearby , const bool bLoadVisitList )
{
	if ( IsValid ( DataComponent ) == false )
	{
		return;
	}

	if ( const FIntPoint NewCenterChunkIndex = FIntPoint ( RegionIndex , ChunkIndex ) ; CurrentCenterChunkIndex != NewCenterChunkIndex )
	{
		//UE_LOG ( LogTemp , Warning , TEXT("Chunk Pos is %s") , *NewPlayerChunkPos.ToString() );

		CurrentCenterChunkIndex = NewCenterChunkIndex;

		UnloadChunkByDistance ( );

		if ( bLoadNearby )
		{
			LoadChunkByNearbyPoint ( );
		}

		if ( bLoadVisitList )
		{
			LoadChunkByVisitList ( );
		}
	}
}

void ULPPChunkManager::OnDataTagChanged ( const int32 RegionIndex , const int32 ChunkIndex , const int32 DataIndex , const FGameplayTag& OldTag , const FGameplayTag& NewTag )
{
	if ( IsValid ( ChunkIndexTranslator ) == false )
	{
		return;
	}

	if ( TObjectPtr < ULPPChunkController >* LoadedChunk = LoadedChunkMap.Find ( FIntPoint ( RegionIndex , ChunkIndex ) ) ; LoadedChunk != nullptr )
	{
		// Notify Chunk Data Has Changed
		LoadedChunk->Get ( )->UpdateChunk ( );
	}

	{
		const FIntVector& DataGridSize = ChunkIndexTranslator->GetDataGridSize ( );

		const FIntVector DataLocation = ULFPGridLibrary::ToGridLocation ( DataIndex , DataGridSize );

		for ( const TArray < FIntVector > EdgeDirectionList = ULFPGridLibrary::GetGridEdgeDirection ( DataLocation , DataGridSize ) ; const auto& GridEdgeDirection : EdgeDirectionList )
		{
			const FIntVector& TargetIndex = ChunkIndexTranslator->ToDataGridIndex ( ChunkIndexTranslator->ToDataGridPosition ( FIntVector ( RegionIndex , ChunkIndex , DataIndex ) ) + GridEdgeDirection );

			if ( TargetIndex != FIntVector::NoneValue )
			{
				if ( TObjectPtr < ULPPChunkController >* LoadedChunk = LoadedChunkMap.Find ( FIntPoint ( TargetIndex.X , TargetIndex.Y ) ) ; LoadedChunk != nullptr )
				{
					// Notify Chunk Data Has Changed
					LoadedChunk->Get ( )->UpdateChunk ( );
				}
			}
		}
	}

	MarkChunkUpdate ( RegionIndex , ChunkIndex );
}

void ULPPChunkManager::OnChunkInitialized ( const int32 RegionIndex , const int32 ChunkIndex )
{
	if ( IsValid ( DataComponent ) == false )
	{
		return;
	}

	MarkChunkUpdate ( RegionIndex , ChunkIndex );
}

void ULPPChunkManager::OnChunkUninitialized ( const int32 RegionIndex , const int32 ChunkIndex )
{
	if ( IsValid ( DataComponent ) == false )
	{
		return;
	}

	MarkChunkUpdate ( RegionIndex , ChunkIndex );
}

void ULPPChunkManager::ReserveChunkActor ( )
{
	if ( IsValid ( GetWorld ( ) ) == false )
	{
		return;
	}

	// Reset All Loaded Chunk
	{
		for ( const auto& LoadedChunk : LoadedChunkMap )
		{
			if ( IsValid ( LoadedChunk.Value ) == false )
			{
				continue;
			}

			LoadedChunk.Value->ClearLODIndex ( );

			AvailableChunkList.Add ( LoadedChunk.Value );
		}

		LoadedChunkMap.Empty ( );
	}

	const int32 CurrentReserveAmount = AvailableChunkList.Num ( );

	const int32 ChunkLoadRange = MaxLODDistance + MaxLODDistance + 1;
	const int32 ReserveAmount  = ChunkLoadRange * ChunkLoadRange * ChunkLoadRange;

	if ( CurrentReserveAmount == ReserveAmount )
	{
		return;
	}

	if ( CurrentReserveAmount > ReserveAmount )
	{
		for ( int32 DestroyCounter = 0 ; DestroyCounter < CurrentReserveAmount - ReserveAmount ; ++DestroyCounter )
		{
			const TObjectPtr < ULPPChunkController > ChunkPtr = AvailableChunkList.Pop ( EAllowShrinking::No );

			if ( IsValid ( ChunkPtr ) == false || IsValid ( ChunkPtr->GetOwner ( ) ) == false )
			{
				continue;
			}

			ChunkPtr->GetOwner ( )->Destroy ( );
		}
	}
	else
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = GetOwner ( );

		for ( int32 SpawnCounter = CurrentReserveAmount ; SpawnCounter < ReserveAmount ; ++SpawnCounter )
		{
			AActor* SpawnedPtr = GetWorld ( )->SpawnActor < AActor > ( ChunkActorClass , SpawnParameters );

			if ( IsValid ( SpawnedPtr ) == false )
			{
				continue;
			}

			ULPPChunkController* ComponentPtr = SpawnedPtr->FindComponentByClass < ULPPChunkController > ( );

			if ( IsValid ( ComponentPtr ) == false )
			{
				continue;
			}

			AvailableChunkList.Add ( ComponentPtr );
		}
	}
}

void ULPPChunkManager::MarkChunkUpdate ( const int32 RegionIndex , const int32 ChunkIndex )
{
	ChunkUpdateList.Add ( FIntPoint ( RegionIndex , ChunkIndex ) );
}

void ULPPChunkManager::LoadChunkByNearbyPoint ( )
{
	if ( IsValid ( ChunkIndexTranslator ) == false )
	{
		return;
	}

	if ( CurrentCenterChunkIndex != FIntPoint::NoneValue )
	{
		const FIntVector CurrentPlayerChunkPos = ChunkIndexTranslator->ToChunkGridPosition ( CurrentCenterChunkIndex );

		for ( int32 OffsetZ = -NearbyDistance ; OffsetZ <= NearbyDistance ; ++OffsetZ )
		{
			for ( int32 OffsetY = -NearbyDistance ; OffsetY <= NearbyDistance ; ++OffsetY )
			{
				for ( int32 OffsetX = -NearbyDistance ; OffsetX <= NearbyDistance ; ++OffsetX )
				{
					const FIntVector LoadChunkPos   = FIntVector ( OffsetX , OffsetY , OffsetZ ) + CurrentPlayerChunkPos;
					const FIntPoint  LoadChunkIndex = ChunkIndexTranslator->ToChunkGridIndex ( LoadChunkPos );

					if ( LoadedChunkMap.Contains ( LoadChunkIndex ) )
					{
						LoadedChunkMap.FindChecked ( LoadChunkIndex )->SetLODIndex ( 0 );
					}
					else if ( AvailableChunkList.IsEmpty ( ) == false )
					{
						if ( ULPPChunkController* TargetComponent = AvailableChunkList.Pop ( EAllowShrinking::No ) ; IsValid ( TargetComponent ) )
						{
							TargetComponent->SetChunkIndex ( LoadChunkIndex.X , LoadChunkIndex.Y );
							TargetComponent->SetLODIndex ( 0 );

							LoadedChunkMap.Add ( LoadChunkIndex , TargetComponent );
						}
					}
				}
			}
		}
	}
}

void ULPPChunkManager::LoadChunkByVisitList ( )
{
	// Reset Iterate Data
	{
		VisitedChunkList.Reset ( );
		NextVisitableChunkList.Reset ( );
	}

	FLPP_VisitableChunkData& PlayerVisitChunk = NextVisitableChunkList.Add_GetRef ( FLPP_VisitableChunkData ( ) );

	PlayerVisitChunk.RegionIndex        = CurrentCenterChunkIndex.X;
	PlayerVisitChunk.ChunkIndex         = CurrentCenterChunkIndex.Y;
	PlayerVisitChunk.FromDirectionIndex = INDEX_NONE;
	PlayerVisitChunk.Step               = 0;

	IterateVisitableChunkList ( );
}

void ULPPChunkManager::UnloadChunkByDistance ( )
{
	if ( IsValid ( ChunkIndexTranslator ) == false )
	{
		return;
	}

	// Unload Old Chunk Base On Distance
	if ( CurrentCenterChunkIndex != FIntPoint::NoneValue )
	{
		const FIntVector CurrentPlayerChunkPos = ChunkIndexTranslator->ToChunkGridPosition ( CurrentCenterChunkIndex );

		TArray < FIntPoint > UnloadList;

		for ( const auto& LoadedChunkData : LoadedChunkMap )
		{
			if ( IsValid ( LoadedChunkData.Value ) == false )
			{
				UnloadList.Add ( LoadedChunkData.Key );

				continue;
			}

			const FIntVector CurrentChunkPos = ChunkIndexTranslator->ToChunkGridPosition ( LoadedChunkData.Key );

			if ( const int32 DistanceToPlayer = ( CurrentChunkPos - CurrentPlayerChunkPos ).GetAbsMax ( ) ; DistanceToPlayer > MaxLODDistance )
			{
				UnloadList.Add ( LoadedChunkData.Key );
			}
		}

		for ( const FIntPoint& UnloadID : UnloadList )
		{
			TObjectPtr < ULPPChunkController > RemovedComponent = nullptr;

			LoadedChunkMap.RemoveAndCopyValue ( UnloadID , RemovedComponent );

			if ( IsValid ( RemovedComponent ) )
			{
				RemovedComponent->ClearLODIndex ( );

				AvailableChunkList.Add ( RemovedComponent );
			}
		}
	}
}

void ULPPChunkManager::UnloadChunkByVisitList ( )
{
	if ( IsValid ( ChunkIndexTranslator ) == false )
	{
		return;
	}

	// Unload Old Chunk Base On Visit List
	if ( CurrentCenterChunkIndex != FIntPoint::NoneValue )
	{
		const uint8 UnloadDistance = MaxLODDistance >> 1;

		TArray < FIntPoint > UnloadList;

		UnloadList.Reserve ( LoadedChunkMap.Num ( ) );

		const FIntVector CurrentPlayerChunkPos = ChunkIndexTranslator->ToChunkGridPosition ( CurrentCenterChunkIndex );

		for ( const auto& LoadedChunkData : LoadedChunkMap )
		{
			if ( IsValid ( LoadedChunkData.Value ) == false )
			{
				UnloadList.Add ( LoadedChunkData.Key );
			}

			const FIntVector CurrentChunkPos = ChunkIndexTranslator->ToChunkGridPosition ( LoadedChunkData.Key );

			if ( const int32 DistanceToPlayer = ( CurrentChunkPos - CurrentPlayerChunkPos ).GetAbsMax ( ) ; DistanceToPlayer > UnloadDistance && VisitedChunkList.Contains ( LoadedChunkData.Key ) == false )
			{
				UnloadList.Add ( LoadedChunkData.Key );
			}
		}

		for ( const FIntPoint& UnloadID : UnloadList )
		{
			TObjectPtr < ULPPChunkController > RemovedComponent = nullptr;

			LoadedChunkMap.RemoveAndCopyValue ( UnloadID , RemovedComponent );

			if ( IsValid ( RemovedComponent ) )
			{
				RemovedComponent->ClearLODIndex ( );

				AvailableChunkList.Add ( RemovedComponent );
			}
		}
	}
}

void ULPPChunkManager::IterateVisitableChunkList ( )
{
	if ( IsValid ( DataComponent ) == false )
	{
		return;
	}

	if ( ChunkUpdateList.IsEmpty ( ) == false )
	{
		return;
	}

	ULPPGridDataLibrary::IterateVisitableChunkList (
	                                                DataComponent ,
	                                                ChunkIndexTranslator ,
	                                                CurrentCenterChunkIndex.X ,
	                                                CurrentCenterChunkIndex.Y ,
	                                                ConnectionMetaTag ,
	                                                VisibleWeight ,
	                                                MaxLODDistance ,
	                                                MaxLODDistance >> 1 ,
	                                                VisitedChunkList ,
	                                                NextVisitableChunkList
	                                               );

	if ( NextVisitableChunkList.IsEmpty ( ) )
	{
		UnloadChunkByVisitList ( );
	}
}

bool ULPPChunkManager::IsChunkVisibleToPlayer ( const int32 RegionIndex , const int32 ChunkIndex ) const
{
	if ( IsValid ( DataComponent ) == false )
	{
		return false;
	}

	return ULPPGridDataLibrary::LineTraceChunkVisibleToCenterIndex (
	                                                                DataComponent ,
	                                                                ChunkIndexTranslator ,
	                                                                CurrentCenterChunkIndex.X ,
	                                                                CurrentCenterChunkIndex.Y ,
	                                                                RegionIndex ,
	                                                                ChunkIndex ,
	                                                                ConnectionMetaTag
	                                                               );
}
