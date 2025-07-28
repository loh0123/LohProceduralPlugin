// Fill out your copyright notice in the Description page of Project Settings.


#include "Library/LPPGridDataLibrary.h"

#include "Components/LFPChunkedTagDataComponent.h"
#include "Data/LFPChunkedIndexTranslator.h"
#include "Math/LFPGridLibrary.h"

bool ULPPGridDataLibrary::SetChunkConnectionMetaData ( ULFPChunkedTagDataComponent* DataComponent , const ULFPChunkedIndexTranslator* IndexTranslator , const int32 RegionIndex , const int32 ChunkIndex , const FGameplayTag& BlockTag , const FGameplayTag& ConnectionMetaTag , const bool bDebug )
{
	if ( IsValid ( DataComponent ) == false )
	{
		return false;
	}

	if ( IsValid ( IndexTranslator ) == false )
	{
		return false;
	}

	if ( BlockTag.IsValid ( ) == false || ConnectionMetaTag.IsValid ( ) == false )
	{
		return false;
	}

	if ( DataComponent->IsChunkIndexValid ( RegionIndex , ChunkIndex ) == false )
	{
		return false;
	}

	// Remove Meta Data First
	DataComponent->RemoveChunkMeta ( RegionIndex , ChunkIndex , ConnectionMetaTag );

	if ( DataComponent->IsChunkInitialized ( RegionIndex , ChunkIndex ) == false )
	{
		return false;
	}

	if ( DataComponent->GetDataTagList ( RegionIndex , ChunkIndex ).IsEmpty ( ) )
	{
		TArray < uint8 > RecastData;
		{
			RecastData.SetNum ( 2 );

			*( reinterpret_cast < uint16* > ( RecastData.GetData ( ) ) ) = UINT16_MAX;
		}

		DataComponent->SetChunkMeta ( RegionIndex , ChunkIndex , ConnectionMetaTag , RecastData );

		return false;
	}

	TSet < int32 > CheckedList;
	TSet < int32 > CurrentList;
	TSet < int32 > NextList;

	CheckedList.Reserve ( DataComponent->GetDataIndexSize ( ) );
	CurrentList.Reserve ( DataComponent->GetDataIndexSize ( ) );
	NextList.Reserve ( DataComponent->GetDataIndexSize ( ) );

	uint16 ChunkConnectionFlag = 0;

	for ( int32 IterateDataIndex = 0 ; IterateDataIndex < DataComponent->GetDataIndexSize ( ) ; ++IterateDataIndex )
	{
		if ( CheckedList.Contains ( IterateDataIndex ) )
		{
			continue;
		}

		if ( DataComponent->GetDataTag ( RegionIndex , ChunkIndex , IterateDataIndex ).MatchesTag ( BlockTag ) )
		{
			continue;
		}

		NextList.Add ( IterateDataIndex );

		uint8 DirectionFlag = 0;

		// Flood Fill Algo
		while ( NextList.IsEmpty ( ) == false )
		{
			CurrentList = NextList;
			{
				CheckedList.Append ( NextList );
				NextList.Reset ( );
			}

			for ( const int32 CurrentDataIndex : CurrentList )
			{
				const FIntVector CurrentLocalDataPos = ULFPGridLibrary::ToGridLocation ( CurrentDataIndex , IndexTranslator->GetDataGridSize ( ) );

				for ( int32 CheckIndex = 0 ; CheckIndex < 6 ; ++CheckIndex )
				{
					const int32 CheckDataIndex = ULFPGridLibrary::ToGridIndex ( CurrentLocalDataPos + NLPP_ChunkDataHelper::CheckDirectionList [ CheckIndex ] , IndexTranslator->GetDataGridSize ( ) );

					if ( CheckDataIndex == INDEX_NONE )
					{
						DirectionFlag |= 1 << CheckIndex;
					}
					else if ( DataComponent->GetDataTag ( RegionIndex , ChunkIndex , CheckDataIndex ).MatchesTag ( BlockTag ) == false && CheckedList.Contains ( CheckDataIndex ) == false )
					{
						NextList.Add ( CheckDataIndex );
					}
				}
			}
		}

		for ( int32 ConnectionIndex = 0 ; ConnectionIndex < NLPP_ChunkDataHelper::MappingNum ; ++ConnectionIndex )
		{
			const uint8 MappingID = NLPP_ChunkDataHelper::DirectionIndexToConnectionMappingList [ ConnectionIndex ];

			if ( ( DirectionFlag & MappingID ) == MappingID )
			{
				ChunkConnectionFlag |= 1 << ConnectionIndex;
			}
		}

		if ( bDebug )
		{
			UE_LOG ( LogTemp , Log , TEXT ( "Chunk Direction Flag : %u" ) , DirectionFlag );
		}
	}

	if ( bDebug )
	{
		UE_LOG ( LogTemp , Log , TEXT ( "Chunk Connection Flag : %u" ) , ChunkConnectionFlag );
	}

	TArray < uint8 > RecastData;
	{
		RecastData.SetNum ( 2 );

		*( reinterpret_cast < uint16* > ( RecastData.GetData ( ) ) ) = ChunkConnectionFlag;
	}

	DataComponent->SetChunkMeta ( RegionIndex , ChunkIndex , ConnectionMetaTag , RecastData );

	return true;
}

bool ULPPGridDataLibrary::LineTraceChunkVisibleToCenterIndex ( const ULFPChunkedTagDataComponent* DataComponent , const ULFPChunkedIndexTranslator* IndexTranslator , const int32 CenterRegionIndex , const int32 CenterChunkIndex , const int32 RegionIndex , const int32 ChunkIndex , const FGameplayTag& ConnectionMetaTag )
{
	if ( IsValid ( DataComponent ) == false )
	{
		return false;
	}

	if ( IsValid ( IndexTranslator ) == false )
	{
		return false;
	}

	const FIntPoint CurrentChunkIndex = FIntPoint ( RegionIndex , ChunkIndex );
	const FIntPoint TargetChunkIndex  = FIntPoint ( CenterRegionIndex , CenterChunkIndex );

	TArray < FIntVector > TraceChunkPosList;
	{
		//const FIntVector CurrentChunkPos = IndexTranslator->ToChunkGridPosition ( FIntPoint ( RegionIndex , ChunkIndex ) );
		//const FIntVector TargetChunkPos  = IndexTranslator->ToChunkGridPosition ( FIntPoint ( CenterRegionIndex , CenterChunkIndex ) );

		const FIntVector TargetChunkPos = IndexTranslator->GetDistanceToChunkGridIndex (
		                                                                                TargetChunkIndex ,
		                                                                                CurrentChunkIndex ,
		                                                                                false
		                                                                               );

		const FIntVector RayLine = TargetChunkPos; //TargetChunkPos - CurrentChunkPos;

		// In which direction the ids are incremented.
		const int32 StepX = ( RayLine.X >= 0 ) ? 1 : -1;
		const int32 StepY = ( RayLine.Y >= 0 ) ? 1 : -1;
		const int32 StepZ = ( RayLine.Z >= 0 ) ? 1 : -1;

		double TravelMaxX = ( RayLine.X != 0 ) ? static_cast < double > ( StepX ) / static_cast < double > ( RayLine.X ) : DBL_MAX; //
		double TravelMaxY = ( RayLine.Y != 0 ) ? static_cast < double > ( StepY ) / static_cast < double > ( RayLine.Y ) : DBL_MAX; //
		double TravelMaxZ = ( RayLine.Z != 0 ) ? static_cast < double > ( StepZ ) / static_cast < double > ( RayLine.Z ) : DBL_MAX; //

		const double TravelDeltaX = ( RayLine.X != 0 ) ? FMath::Abs ( 1.0 / static_cast < double > ( RayLine.X ) ) : DBL_MAX;
		const double TravelDeltaY = ( RayLine.Y != 0 ) ? FMath::Abs ( 1.0 / static_cast < double > ( RayLine.Y ) ) : DBL_MAX;
		const double TravelDeltaZ = ( RayLine.Z != 0 ) ? FMath::Abs ( 1.0 / static_cast < double > ( RayLine.Z ) ) : DBL_MAX;

		FIntVector IteratePos ( 0 );

		TraceChunkPosList.Add ( IteratePos );

		const int32 MaxTraceAmount = FMath::Abs ( RayLine.X ) + FMath::Abs ( RayLine.Y ) + FMath::Abs ( RayLine.Z );

		for ( int32 TraceIndex = 0 ; TraceIndex < MaxTraceAmount && TargetChunkPos != IteratePos ; ++TraceIndex )
		{
			if ( TravelMaxX < TravelMaxY )
			{
				if ( TravelMaxX < TravelMaxZ )
				{
					IteratePos.X += StepX;
					TravelMaxX += TravelDeltaX;
				}
				else
				{
					IteratePos.Z += StepZ;
					TravelMaxZ += TravelDeltaZ;
				}
			}
			else
			{
				if ( TravelMaxY < TravelMaxZ )
				{
					IteratePos.Y += StepY;
					TravelMaxY += TravelDeltaY;
				}
				else
				{
					IteratePos.Z += StepZ;
					TravelMaxZ += TravelDeltaZ;
				}
			}

			TraceChunkPosList.Add ( IteratePos );
		}
	}

	if ( TraceChunkPosList.IsValidIndex ( 1 ) == false )
	{
		return true;
	}

	int32 FromDirectionIndex = NLPP_ChunkDataHelper::ReverseDirectionIndexList [ NLPP_ChunkDataHelper::CheckDirectionList.IndexOfByKey ( TraceChunkPosList [ 1 ] - TraceChunkPosList [ 0 ] ) ];

	for ( int32 TraceIndex = 1 ; TraceIndex + 1 < TraceChunkPosList.Num ( ) ; ++TraceIndex )
	{
		const FIntPoint TracedChunkIndex = IndexTranslator->AddOffsetToChunkGridIndex ( CurrentChunkIndex , TraceChunkPosList [ TraceIndex ] );

		if ( TracedChunkIndex == FIntPoint::NoneValue )
		{
			return false;
		}

		const TArray < uint8 > ChunkConnectionDataList = DataComponent->GetChunkMeta ( TracedChunkIndex.X , TracedChunkIndex.Y , ConnectionMetaTag ).AsList ( );

		// Data Not Calculate
		if ( ChunkConnectionDataList.IsValidIndex ( 1 ) == false )
		{
			return false;
		}

		const int32 ToDirectionIndex = NLPP_ChunkDataHelper::CheckDirectionList.IndexOfByKey ( TraceChunkPosList [ TraceIndex + 1 ] - TraceChunkPosList [ TraceIndex ] );

		const uint16 ChunkConnectionMask = *( reinterpret_cast < const uint16* > ( ChunkConnectionDataList.GetData ( ) ) );

		const uint8  ConnectionID   = 1 << FromDirectionIndex | 1 << ToDirectionIndex;
		const uint16 ConnectionMask = NLPP_ChunkDataHelper::ConnectionToDirectionMappingList.FindChecked ( ConnectionID );

		// Connection Is Close?
		if ( ( ConnectionMask & ChunkConnectionMask ) != ConnectionMask )
		{
			return false;
		}

		FromDirectionIndex = NLPP_ChunkDataHelper::ReverseDirectionIndexList [ NLPP_ChunkDataHelper::CheckDirectionList.IndexOfByKey ( TraceChunkPosList [ TraceIndex + 1 ] - TraceChunkPosList [ TraceIndex ] ) ];
	}

	return true;
}

void ULPPGridDataLibrary::IterateVisitableChunkList ( const ULFPChunkedTagDataComponent* DataComponent , const ULFPChunkedIndexTranslator* IndexTranslator , const int32 CenterRegionIndex , const int32 CenterChunkIndex , const FGameplayTag& ConnectionMetaTag , const uint8 VisibleStepSize , const uint8 MaxStepSize , const uint8 LineTraceStep , TMap < FIntPoint , int32 >& VisitedChunkList , TArray < FLPP_VisitableChunkData >& NextVisitableChunkList )
{
	if ( IsValid ( DataComponent ) == false )
	{
		return;
	}

	if ( IsValid ( IndexTranslator ) == false )
	{
		return;
	}

	if ( NextVisitableChunkList.IsEmpty ( ) )
	{
		return;
	}

	TArray < FLPP_VisitableChunkData > CurrentVisitableChunkList = NextVisitableChunkList;
	{
		NextVisitableChunkList.Reset ( );
	}

	for ( const FLPP_VisitableChunkData& VisitableChunk : CurrentVisitableChunkList )
	{
		const int32 VisitableWeight = VisitableChunk.Step + VisitableChunk.VisibleStep;

		// Is Already Visited
		if ( int32* CurrentChunkWeight = VisitedChunkList.Find ( FIntPoint ( VisitableChunk.RegionIndex , VisitableChunk.ChunkIndex ) ) ; CurrentChunkWeight != nullptr && *CurrentChunkWeight <= VisitableWeight )
		{
			continue;
		}

		VisitedChunkList.Add ( FIntPoint ( VisitableChunk.RegionIndex , VisitableChunk.ChunkIndex ) , VisitableWeight );

		int32 NextVisibleStep = VisitableChunk.VisibleStep;

		// Chunk Is Not Visible To Player?
		if ( VisitableChunk.Step >= LineTraceStep && LineTraceChunkVisibleToCenterIndex ( DataComponent , IndexTranslator , CenterRegionIndex , CenterChunkIndex , VisitableChunk.RegionIndex , VisitableChunk.ChunkIndex , ConnectionMetaTag ) == false )
		{
			NextVisibleStep += VisibleStepSize;
		}

		// Is Max Distance?
		if ( MaxStepSize - 1 <= VisitableChunk.Step + NextVisibleStep )
		{
			continue;
		}

		const FIntPoint        CurrentChunkIndex       = FIntPoint ( VisitableChunk.RegionIndex , VisitableChunk.ChunkIndex );
		const TArray < uint8 > ChunkConnectionDataList = DataComponent->GetChunkMeta ( VisitableChunk.RegionIndex , VisitableChunk.ChunkIndex , ConnectionMetaTag ).AsList ( );

		// Data Not Calculate
		if ( ChunkConnectionDataList.IsValidIndex ( 1 ) == false )
		{
			continue;
		}

		const uint16 ChunkConnectionMask = *( reinterpret_cast < const uint16* > ( ChunkConnectionDataList.GetData ( ) ) );

		if ( ChunkConnectionMask == 0 )
		{
			continue;
		}

		if ( VisitableChunk.FromDirectionIndex != INDEX_NONE )
		{
			for ( int32 DirectionIndex = 0 ; DirectionIndex < 6 ; ++DirectionIndex )
			{
				// Is Backward Direction?
				if ( VisitableChunk.FromDirectionIndex == DirectionIndex )
				{
					continue;
				}

				const uint8  ConnectionID   = 1 << VisitableChunk.FromDirectionIndex | 1 << DirectionIndex;
				const uint16 ConnectionMask = NLPP_ChunkDataHelper::ConnectionToDirectionMappingList.FindChecked ( ConnectionID );

				// Connection Is Open?
				if ( ( ConnectionMask & ChunkConnectionMask ) == ConnectionMask )
				{
					const FIntPoint CheckChunkIndex = IndexTranslator->AddOffsetToChunkGridIndex ( CurrentChunkIndex , NLPP_ChunkDataHelper::CheckDirectionList [ DirectionIndex ] );

					// Is Chunk Index Valid
					if ( CheckChunkIndex != FIntPoint::NoneValue )
					{
						FLPP_VisitableChunkData& NewVisitChunk = NextVisitableChunkList.Add_GetRef ( FLPP_VisitableChunkData ( ) );

						NewVisitChunk.RegionIndex        = CheckChunkIndex.X;
						NewVisitChunk.ChunkIndex         = CheckChunkIndex.Y;
						NewVisitChunk.FromDirectionIndex = NLPP_ChunkDataHelper::ReverseDirectionIndexList [ DirectionIndex ];
						NewVisitChunk.Step               = VisitableChunk.Step + 1;
						NewVisitChunk.VisibleStep        = NextVisibleStep;
					}
				}
			}
		}
		else
		{
			uint8 ConnectionID = 0;

			for ( int32 ConnectionIndex = 0 ; ConnectionIndex < NLPP_ChunkDataHelper::MappingNum ; ++ConnectionIndex )
			{
				if ( ChunkConnectionMask & ( 1 << ConnectionIndex ) )
				{
					ConnectionID |= NLPP_ChunkDataHelper::DirectionIndexToConnectionMappingList [ ConnectionIndex ];
				}
			}

			for ( int32 DirectionIndex = 0 ; DirectionIndex < 6 ; ++DirectionIndex )
			{
				if ( ConnectionID & 1 << DirectionIndex )
				{
					const FIntPoint CheckChunkIndex = IndexTranslator->AddOffsetToChunkGridIndex ( CurrentChunkIndex , NLPP_ChunkDataHelper::CheckDirectionList [ DirectionIndex ] );

					// Is Chunk Index Valid
					if ( CheckChunkIndex != FIntPoint::NoneValue )
					{
						FLPP_VisitableChunkData& NewVisitChunk = NextVisitableChunkList.Add_GetRef ( FLPP_VisitableChunkData ( ) );

						NewVisitChunk.RegionIndex        = CheckChunkIndex.X;
						NewVisitChunk.ChunkIndex         = CheckChunkIndex.Y;
						NewVisitChunk.FromDirectionIndex = NLPP_ChunkDataHelper::ReverseDirectionIndexList [ DirectionIndex ];
						NewVisitChunk.Step               = VisitableChunk.Step + 1;
						NewVisitChunk.VisibleStep        = NextVisibleStep;
					}
				}
			}
		}
	}
}
