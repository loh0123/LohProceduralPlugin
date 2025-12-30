// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Util/ProgressCancel.h"
#include "LPPDynamicMeshLibrary.generated.h"

/**
 * 
 */
UCLASS ( )
class LOHPROCEDURALPLUGIN_API ULPPDynamicMeshLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY ( )

public:

	// Modify to use ParallelFor. Copy from dynamic mesh
	static void BuildDynamicMeshDistanceField ( FDistanceFieldVolumeData& OutData , FProgressCancel& Progress , const UE::Geometry::FDynamicMesh3& Mesh , const bool bGenerateAsIfTwoSided , const float CurrentDistanceFieldResolutionScale );
};
