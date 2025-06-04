// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "LPPChunkedDynamicMesh.h"
#include "MeshCardBuild.h"
#include "RHICommandList.h"
#include "Components/BaseDynamicMeshSceneProxy.h"

/**
 * 
 */
class FLPPChunkedDynamicMeshProxy final : public FBaseDynamicMeshSceneProxy
{
	using FIndex2i = UE::Geometry::FIndex2i;
	using FIndex3i = UE::Geometry::FIndex3i;

private:

	FMaterialRelevance MaterialRelevance;

	// note: FBaseDynamicMeshSceneProxy owns and will destroy these
	FMeshRenderBufferSet* RenderBufferSet;

public:

	/** Component that created this proxy (is there a way to look this up?) */
	ULPPChunkedDynamicMesh* ParentComponent;

protected:

	TSharedPtr < FDistanceFieldVolumeData > DistanceFieldData;

	TUniquePtr < FCardRepresentationData > MeshCardsData;

public:

	FLPPChunkedDynamicMeshProxy ( ULPPChunkedDynamicMesh* Component ) : FBaseDynamicMeshSceneProxy ( Component )
	                                                                    , MaterialRelevance ( Component->GetMaterialRelevance ( GetScene ( ).GetFeatureLevel ( ) ) )
	                                                                    , RenderBufferSet ( nullptr )
	                                                                    , ParentComponent ( Component )
	                                                                    , DistanceFieldData ( nullptr )
	{
	}


	virtual void GetActiveRenderBufferSets ( TArray < FMeshRenderBufferSet* >& Buffers ) const override
	{
		Buffers = { RenderBufferSet };
	}


	void Initialize ( )
	{
		// allocate buffer sets based on materials
		ensure ( RenderBufferSet == nullptr );
		RenderBufferSet           = AllocateNewRenderBufferSet ( );
		RenderBufferSet->Material = ParentComponent->GetMaterial ( 0 );

		if ( RenderBufferSet->Material == nullptr )
		{
			RenderBufferSet->Material = UMaterial::GetDefaultMaterial ( MD_Surface );
		}

		InitializeSingleBufferSet ( RenderBufferSet );
	}


	TUniqueFunction < void  ( int , int , int , const FVector3f& , FVector3f& , FVector3f& ) > MakeTangentsFunc ( bool bSkipAutoCompute = false )
	{
		const FDynamicMesh3* RenderMesh = ParentComponent->GetMesh ( );
		// If the RenderMesh has tangents, use them. Otherwise we fall back to the orthogonal basis, below.
		if ( RenderMesh && RenderMesh->HasAttributes ( ) && RenderMesh->Attributes ( )->HasTangentSpace ( ) )
		{
			UE::Geometry::FDynamicMeshTangents Tangents ( RenderMesh );
			return [Tangents] ( int VertexID , int TriangleID , int TriVtxIdx , const FVector3f& Normal , FVector3f& TangentX , FVector3f& TangentY ) -> void
			{
				Tangents.GetTangentVectors ( TriangleID , TriVtxIdx , Normal , TangentX , TangentY );
			};
		}

		// fallback to orthogonal basis
		return [] ( int VertexID , int TriangleID , int TriVtxIdx , const FVector3f& Normal , FVector3f& TangentX , FVector3f& TangentY ) -> void
		{
			UE::Geometry::VectorUtil::MakePerpVectors ( Normal , TangentX , TangentY );
		};
	}


	/**
	 * Initialize a single set of mesh buffers for the entire mesh
	 */
	void InitializeSingleBufferSet ( FMeshRenderBufferSet* RenderBuffers )
	{
		const FDynamicMesh3* Mesh = ParentComponent->GetMesh ( );

		// find suitable overlays
		TArray < const FDynamicMeshUVOverlay* , TInlineAllocator < 8 > > UVOverlays;
		const FDynamicMeshNormalOverlay*                                 NormalOverlay = nullptr;
		const FDynamicMeshColorOverlay*                                  ColorOverlay  = nullptr;
		if ( Mesh->HasAttributes ( ) )
		{
			const FDynamicMeshAttributeSet* Attributes = Mesh->Attributes ( );
			NormalOverlay                              = Attributes->PrimaryNormals ( );
			UVOverlays.SetNum ( Attributes->NumUVLayers ( ) );
			for ( int32 k = 0 ; k < UVOverlays.Num ( ) ; ++k )
			{
				UVOverlays [ k ] = Attributes->GetUVLayer ( k );
			}
			ColorOverlay = Attributes->PrimaryColors ( );
		}
		TUniqueFunction < void  ( int , int , int , const FVector3f& , FVector3f& , FVector3f& ) > TangentsFunc = MakeTangentsFunc ( );

		const bool bTrackTriangles = false;
		const bool bParallel       = true;
		InitializeBuffersFromOverlays ( RenderBuffers , Mesh ,
		                                Mesh->TriangleCount ( ) , Mesh->TriangleIndicesItr ( ) ,
		                                UVOverlays , NormalOverlay , ColorOverlay , TangentsFunc ,
		                                bTrackTriangles , bParallel );

		ENQUEUE_RENDER_COMMAND ( FChunbkedDynamicMeshSceneProxyInitializeSingle ) (
		                                                                           [RenderBuffers] ( FRHICommandListImmediate& RHICmdList )
		                                                                           {
			                                                                           RenderBuffers->Upload ( );
		                                                                           } );
	}

public:

	void SetDistanceFieldData ( const TSharedPtr < FDistanceFieldVolumeData >& InDistanceFieldData )
	{
		DistanceFieldData                    = InDistanceFieldData;
		bSupportsDistanceFieldRepresentation = true;

		bAffectDistanceFieldLighting = ParentComponent->bAffectDistanceFieldLighting;

		UpdateVisibleInLumenScene ( );
	}

	void SetLumenData ( const TArray < class FLumenCardBuildData >& InLumenCardData , const FBox& InLumenBound )
	{
		if ( MeshCardsData.IsValid ( ) == false )
		{
			MeshCardsData = MakeUnique < FCardRepresentationData > ( );
		}

		FMeshCardsBuildData& MeshCardData = MeshCardsData.Get ( )->MeshCardsBuildData;

		MeshCardData.bMostlyTwoSided = false;
		MeshCardData.Bounds          = InLumenBound;
		MeshCardData.CardBuildData   = InLumenCardData;

		UpdateVisibleInLumenScene ( );
	}

	virtual const FCardRepresentationData* GetMeshCardRepresentation ( ) const override
	{
		return MeshCardsData.Get ( );
	}

	virtual void GetDistanceFieldAtlasData ( const class FDistanceFieldVolumeData*& OutDistanceFieldData , float& SelfShadowBias ) const override
	{
		if ( DistanceFieldData.IsValid ( ) )
		{
			OutDistanceFieldData = DistanceFieldData.Get ( );
			SelfShadowBias       = 0.0f;
		}
	}

	virtual bool HasDistanceFieldRepresentation ( ) const override
	{
		return CastsDynamicShadow ( ) && AffectsDistanceFieldLighting ( ) && DistanceFieldData.IsValid ( );
	}

	virtual bool HasDynamicIndirectShadowCasterRepresentation ( ) const override
	{
		return bCastsDynamicIndirectShadow && DistanceFieldData.IsValid ( );
	}

public:

	virtual FPrimitiveViewRelevance GetViewRelevance ( const FSceneView* View ) const override
	{
		FPrimitiveViewRelevance Result;

		Result.bDrawRelevance   = IsShown ( View );
		Result.bShadowRelevance = IsShadowCast ( View );

		bool bUseStaticDrawPath  = bPreferStaticDrawPath && AllowStaticDrawPath ( View );
		Result.bDynamicRelevance = !bUseStaticDrawPath;
		Result.bStaticRelevance  = bUseStaticDrawPath;
#if WITH_EDITOR
		//only check these in the editor
		Result.bEditorVisualizeLevelInstanceRelevance = IsEditingLevelInstanceChild ( );
		Result.bEditorStaticSelectionRelevance        = ( IsSelected ( ) || IsHovered ( ) );
#endif


		Result.bRenderInMainPass      = ShouldRenderInMainPass ( );
		Result.bUsesLightingChannels  = GetLightingChannelMask ( ) != GetDefaultLightingChannelMask ( );
		Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
		Result.bRenderCustomDepth     = ShouldRenderCustomDepth ( );
		// Note that this is actually a getter. One may argue that it is poorly named.
		MaterialRelevance.SetPrimitiveViewRelevance ( Result );
		Result.bVelocityRelevance = DrawsVelocity ( ) && Result.bOpaque && Result.bRenderInMainPass;

		return Result;
	}

	virtual void UpdatedReferencedMaterials ( ) override
	{
		FBaseDynamicMeshSceneProxy::UpdatedReferencedMaterials ( );

		// The material relevance may need updating.
		MaterialRelevance = ParentComponent->GetMaterialRelevance ( GetScene ( ).GetFeatureLevel ( ) );
	}


	virtual void GetLightRelevance ( const FLightSceneProxy* LightSceneProxy , bool& bDynamic , bool& bRelevant , bool& bLightMapped , bool& bShadowMapped ) const override
	{
		FPrimitiveSceneProxy::GetLightRelevance ( LightSceneProxy , bDynamic , bRelevant , bLightMapped , bShadowMapped );
	}

	virtual bool CanBeOccluded ( ) const override
	{
		return !MaterialRelevance.bDisableDepthTest;
	}

	virtual uint32 GetMemoryFootprint ( void ) const override { return ( sizeof( *this ) + GetAllocatedSize ( ) ); }

	uint32 GetAllocatedSize ( void ) const { return ( FPrimitiveSceneProxy::GetAllocatedSize ( ) ); }

	virtual SIZE_T GetTypeHash ( ) const override
	{
		static size_t UniquePointer;
		return reinterpret_cast < size_t > ( &UniquePointer );
	}
};
