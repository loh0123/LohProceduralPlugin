// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LPPGridDataLibrary.generated.h"

class ULFPChunkedIndexTranslator;
class ULFPChunkedTagDataComponent;

namespace NLPP_ChunkDataHelper
{
	inline constexpr int32 MappingNum = 15;

	inline const TStaticArray < uint8 , 15 > DirectionIndexToConnectionMappingList =
	{
		1 << 0 | 1 << 1 , // BottomToTop  
		1 << 2 | 1 << 3 , // BackToFront  
		1 << 4 | 1 << 5 , // LeftToRight  
		1 << 0 | 1 << 4 , // BottomToLeft 
		1 << 0 | 1 << 3 , // BottomToFront
		1 << 0 | 1 << 5 , // BottomToRight
		1 << 0 | 1 << 2 , // BottomToBack 
		1 << 2 | 1 << 4 , // BackToLeft   
		1 << 4 | 1 << 3 , // LeftToFront  
		1 << 3 | 1 << 5 , // FrontToRight 
		1 << 5 | 1 << 2 , // RightToBack  
		1 << 1 | 1 << 4 , // TopToLeft    
		1 << 1 | 1 << 3 , // TopToFront   
		1 << 1 | 1 << 5 , // TopToRight   
		1 << 1 | 1 << 2 , // TopToBack    
	};

	inline const TMap < uint8 , uint16 > ConnectionToDirectionMappingList =
	{
		{ DirectionIndexToConnectionMappingList [ 0 ] , 1 << 0 } ,   // BottomToTop  
		{ DirectionIndexToConnectionMappingList [ 1 ] , 1 << 1 } ,   // BackToFront  
		{ DirectionIndexToConnectionMappingList [ 2 ] , 1 << 2 } ,   // LeftToRight  
		{ DirectionIndexToConnectionMappingList [ 3 ] , 1 << 3 } ,   // BottomToLeft 
		{ DirectionIndexToConnectionMappingList [ 4 ] , 1 << 4 } ,   // BottomToFront
		{ DirectionIndexToConnectionMappingList [ 5 ] , 1 << 5 } ,   // BottomToRight
		{ DirectionIndexToConnectionMappingList [ 6 ] , 1 << 6 } ,   // BottomToBack 
		{ DirectionIndexToConnectionMappingList [ 7 ] , 1 << 7 } ,   // BackToLeft   
		{ DirectionIndexToConnectionMappingList [ 8 ] , 1 << 8 } ,   // LeftToFront  
		{ DirectionIndexToConnectionMappingList [ 9 ] , 1 << 9 } ,   // FrontToRight 
		{ DirectionIndexToConnectionMappingList [ 10 ] , 1 << 10 } , // RightToBack  
		{ DirectionIndexToConnectionMappingList [ 11 ] , 1 << 11 } , // TopToLeft    
		{ DirectionIndexToConnectionMappingList [ 12 ] , 1 << 12 } , // TopToFront   
		{ DirectionIndexToConnectionMappingList [ 13 ] , 1 << 13 } , // TopToRight   
		{ DirectionIndexToConnectionMappingList [ 14 ] , 1 << 14 } , // TopToBack    
	};

	inline const TArray CheckDirectionList =
	{
		FIntVector ( 0 , 0 , -1 ) ,
		FIntVector ( 0 , 0 , 1 ) ,
		FIntVector ( -1 , 0 , 0 ) ,
		FIntVector ( 1 , 0 , 0 ) ,
		FIntVector ( 0 , -1 , 0 ) ,
		FIntVector ( 0 , 1 , 0 ) ,
	};

	inline constexpr int32 ReverseDirectionIndexList [ ] =
	{
		1 ,
		0 ,
		3 ,
		2 ,
		5 ,
		4 ,
	};
}

USTRUCT ( BlueprintType )
struct FLPP_VisitableChunkData
{
	GENERATED_BODY ( )

public:

	UPROPERTY ( )
	int32 RegionIndex = INDEX_NONE;
	UPROPERTY ( )
	int32 ChunkIndex = INDEX_NONE;
	UPROPERTY ( )
	int32 FromDirectionIndex = INDEX_NONE;
	UPROPERTY ( )
	uint8 Step = 0;
	UPROPERTY ( )
	uint8 VisibleStep = 0;
};

/**
 * 
 */
UCLASS ( )
class LOHPROCEDURALPLUGIN_API ULPPGridDataLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY ( )

public:

	UFUNCTION ( BlueprintCallable , Category = "Default" )
	static bool SetChunkConnectionMetaData (
		ULFPChunkedTagDataComponent*      DataComponent ,
		const ULFPChunkedIndexTranslator* IndexTranslator ,
		const int32                       RegionIndex ,
		const int32                       ChunkIndex ,
		const FGameplayTag&               BlockTag ,
		const FGameplayTag&               ConnectionMetaTag ,
		const bool                        bDebug = false
		);

	UFUNCTION ( BlueprintCallable , Category = "Default" )
	static bool LineTraceChunkVisibleToCenterIndex (
		const ULFPChunkedTagDataComponent* DataComponent ,
		const ULFPChunkedIndexTranslator*  IndexTranslator ,
		const int32                        CenterRegionIndex ,
		const int32                        CenterChunkIndex ,
		const int32                        RegionIndex ,
		const int32                        ChunkIndex ,
		const FGameplayTag&                ConnectionMetaTag
		);

	UFUNCTION ( BlueprintCallable , Category = "Default" )
	static void IterateVisitableChunkList (
		const ULFPChunkedTagDataComponent*  DataComponent ,
		const ULFPChunkedIndexTranslator*   IndexTranslator ,
		const int32                         CenterRegionIndex ,
		const int32                         CenterChunkIndex ,
		const FGameplayTag&                 ConnectionMetaTag ,
		const uint8                         VisibleStepSize ,
		const uint8                         MaxStepSize ,
		const uint8                         LineTraceStep ,
		TMap < FIntPoint , int32 >&         VisitedChunkList ,
		TArray < FLPP_VisitableChunkData >& NextVisitableChunkList
		);
};
