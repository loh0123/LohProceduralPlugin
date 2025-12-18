// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "LPPChunkManagerInterface.generated.h"

// This class does not need to be modified.
UINTERFACE ( )
class ULPPChunkManagerInterface : public UInterface
{
	GENERATED_BODY ( )
};

/**
 * 
 */
class LOHPROCEDURALPLUGIN_API ILPPChunkManagerInterface
{
	GENERATED_BODY ( )

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:

	UFUNCTION ( BlueprintNativeEvent , Category=ChunkManager )
	FVector         GetChunkLocation ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex ) const;
	virtual FVector GetChunkLocation_Implementation ( const int32 ComponentIndex , const int32 RegionIndex , const int32 ChunkIndex ) const { return FVector::ZeroVector; }

	UFUNCTION ( BlueprintNativeEvent , Category=ChunkManager )
	FIntVector         GetIndexSize ( const int32 ComponentIndex ) const;
	virtual FIntVector GetIndexSize_Implementation ( const int32 ComponentIndex ) const { return FIntVector::ZeroValue; }
};
