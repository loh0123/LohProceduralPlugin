// Fill out your copyright notice in the Description page of Project Settings.


#include "Data/LPPDynamicMeshRenderData.h"

#include "DistanceFieldAtlas.h"
#include "MeshCardBuild.h"
#include "Rendering/NaniteResources.h"


FLPPDynamicMeshRenderData::FLPPDynamicMeshRenderData ( ) : bHasNaniteFallbackMesh ( false )
                                                           , bReadyForStreaming ( false )
{
	ClearNaniteResources ( NaniteResourcesPtr );
}

FLPPDynamicMeshRenderData::~FLPPDynamicMeshRenderData ( )
{
	check ( bIsInitialized == false );

	//if (RayTracingProxy != nullptr)
	//{
	//	delete RayTracingProxy;
	//}
}

void FLPPDynamicMeshRenderData::InitResources ( ERHIFeatureLevel::Type InFeatureLevel , UMeshComponent* Owner )
{
	checkf ( FApp::CanEverRender() || !FPlatformProperties::RequiresCookedData() , TEXT("RenderData should not initialize resources in headless cooked runs") );

	if ( NaniteResourcesPtr.IsValid ( ) )
	{
		NaniteResourcesPtr->InitResources ( Owner );
	}

	//if ( RayTracingProxy != nullptr )
	//{
	//	RayTracingProxy->InitResources ( Owner );
	//}

	//#if RHI_RAYTRACING
	//	if ( IsRayTracingAllowed ( ) && RayTracingProxy != nullptr ) // TODO: Could move most of this to FStaticMeshLODResources::InitResources
	//	{
	//		checkf ( Owner->bSupportRayTracing , TEXT("Unexpected RayTracingProxy on '%s' (support ray tracing is disabled on this UStaticMesh).") , *GetPathNameSafe(Owner) );
	//
	//		ENQUEUE_RENDER_COMMAND ( InitStaticMeshRayTracingGeometry ) (
	//		                                                             [this, Owner] ( FRHICommandListImmediate& RHICmdList )
	//		                                                             {
	//			                                                             FStaticMeshRayTracingProxyLODArray& RayTracingLODs = RayTracingProxy->LODs;
	//
	//			                                                             check ( !RayTracingProxy->bUsingRenderingLODs || RayTracingLODs.Num() == LODResources.Num() );
	//
	//			                                                             RayTracingGeometryGroupHandle = GRayTracingGeometryManager->RegisterRayTracingGeometryGroup ( RayTracingLODs.Num ( ) , RayTracingProxy->bUsingRenderingLODs ? CurrentFirstLODIdx : 0 );
	//
	//			                                                             for ( int32 LODIndex = 0 ; LODIndex < RayTracingLODs.Num ( ) ; ++LODIndex )
	//			                                                             {
	//				                                                             FStaticMeshRayTracingProxyLOD& RayTracingLOD = RayTracingLODs [ LODIndex ];
	//
	//				                                                             // Skip LODs that have their render data stripped
	//				                                                             if ( RayTracingLOD.VertexBuffers->StaticMeshVertexBuffer.GetNumVertices ( ) > 0 )
	//				                                                             {
	//					                                                             RayTracingLOD.RayTracingGeometry->GroupHandle = RayTracingGeometryGroupHandle;
	//
	//					                                                             const bool bHasStreamableData = RayTracingLOD.StreamableData.GetBulkDataSize ( ) > 0 && RayTracingLOD.GetRequestSize ( ) > 0;
	//
	//					                                                             if ( bHasStreamableData )
	//					                                                             {
	//						                                                             checkf ( RayTracingLOD.bOwnsRayTracingGeometry , TEXT("Unexpected StreamableData on '%s' (StreamableData is only expected when ray tracing proxy owns the geometry).") , *GetPathNameSafe(Owner) );
	//						                                                             checkf ( !RayTracingLOD.bBuffersInlined , TEXT("RayTracing LOD with streamable data shouldn't have buffers inlined") );
	//					                                                             }
	//
	//					                                                             if ( bHasStreamableData || LODIndex < CurrentFirstLODIdx )
	//					                                                             {
	//						                                                             RayTracingLOD.RayTracingGeometry->Initializer.Type = ERayTracingGeometryInitializerType::StreamingDestination;
	//					                                                             }
	//
	//					                                                             RayTracingLOD.RayTracingGeometry->LODIndex = LODIndex;
	//					                                                             RayTracingLOD.RayTracingGeometry->InitResource ( RHICmdList );
	//
	//					                                                             if ( bHasStreamableData )
	//					                                                             {
	//						                                                             ( ( FRayTracingGeometryManager* ) GRayTracingGeometryManager )->SetRayTracingGeometryStreamingData (
	//						                                                                                                                                                                 RayTracingLOD.RayTracingGeometry ,
	//						                                                                                                                                                                 RayTracingLOD );
	//					                                                             }
	//				                                                             }
	//			                                                             }
	//		                                                             }
	//		                                                            );
	//	}
	//#endif // RHI_RAYTRACING

	ENQUEUE_RENDER_COMMAND ( CmdSetLPPDynamicMeshReadyForStreaming ) (
	                                                                  [this, Owner] ( FRHICommandListImmediate& )
	                                                                  {
		                                                                  bReadyForStreaming = true;
	                                                                  } );
	bIsInitialized = true;
}

void FLPPDynamicMeshRenderData::GetResourceSizeEx ( FResourceSizeEx& CumulativeResourceSize ) const
{
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes ( sizeof( *this ) );

	CumulativeResourceSize.AddUnknownMemoryBytes ( MaterialMapping.GetAllocatedSize ( ) );

	CumulativeResourceSize.AddUnknownMemoryBytes ( MeshData.GetByteCount ( ) );

	GetNaniteResourcesSizeEx ( NaniteResourcesPtr , CumulativeResourceSize );

	if ( LumenCardData.IsValid ( ) )
	{
		LumenCardData->GetResourceSizeEx ( CumulativeResourceSize );
	}

	if ( DistanceFieldPtr.IsValid ( ) )
	{
		DistanceFieldPtr->GetResourceSizeEx ( CumulativeResourceSize );
	}
}

SIZE_T FLPPDynamicMeshRenderData::GetCPUAccessMemoryOverhead ( ) const
{
	return MeshData.GetByteCount ( );
}

void FLPPDynamicMeshRenderData::ReleaseResources ( )
{
	const bool bWasInitialized = bIsInitialized;

	bIsInitialized = false;

	if ( NaniteResourcesPtr.IsValid ( ) )
	{
		NaniteResourcesPtr->ReleaseResources ( );
	}

	//if ( RayTracingProxy != nullptr )
	//{
	//	RayTracingProxy->ReleaseResources ( );
	//}

	//#if RHI_RAYTRACING
	//	if ( bWasInitialized && IsRayTracingAllowed ( ) )
	//	{
	//		ENQUEUE_RENDER_COMMAND ( CmdReleaseRayTracingGeometryGroup ) (
	//		                                                              [this] ( FRHICommandListImmediate& )
	//		                                                              {
	//			                                                              if ( RayTracingGeometryGroupHandle != INDEX_NONE )
	//			                                                              {
	//				                                                              GRayTracingGeometryManager->ReleaseRayTracingGeometryGroup ( RayTracingGeometryGroupHandle );
	//				                                                              RayTracingGeometryGroupHandle = INDEX_NONE;
	//			                                                              }
	//		                                                              } );
	//	}
	//#endif
}

//void FLPPDynamicMeshRenderData::InitializeRayTracingRepresentation ( )
//{
//	const bool bProxyOwnsRayTracingGeometry = IsRayTracingEnableOnDemandSupported ( );
//
//	const int32 NumLODs = LODResources.Num ( );
//
//	if ( !bProxyOwnsRayTracingGeometry )
//	{
//		for ( int32 LODIndex = 0 ; LODIndex < NumLODs ; ++LODIndex )
//		{
//			FStaticMeshLODResources& LODModel = LODResources [ LODIndex ];
//
//			checkf ( LODModel.RayTracingGeometry == nullptr , TEXT("LODModel.RayTracingGeometry expected to be null. Was the static mesh ray tracing representation already initialized?") );
//			LODModel.RayTracingGeometry = new FRayTracingGeometry ( );
//		}
//	}
//
//	checkf ( RayTracingProxy == nullptr , TEXT("RayTracingProxy expected to be null. Was the static mesh ray tracing representation already initialized?") );
//	RayTracingProxy                      = new FStaticMeshRayTracingProxy ( );
//	RayTracingProxy->bUsingRenderingLODs = true;
//
//	TIndirectArray < FStaticMeshRayTracingProxyLOD >& RayTracingLODs = RayTracingProxy->LODs;
//	check ( RayTracingLODs.IsEmpty() );
//	RayTracingLODs.Reserve ( NumLODs );
//
//	for ( int32 LODIndex = 0 ; LODIndex < NumLODs ; ++LODIndex )
//	{
//		FStaticMeshLODResources& LODModel = LODResources [ LODIndex ];
//
//		FStaticMeshRayTracingProxyLOD* RayTracingLOD = new FStaticMeshRayTracingProxyLOD;
//
//		RayTracingLOD->bOwnsRayTracingGeometry = bProxyOwnsRayTracingGeometry;
//		RayTracingLOD->RayTracingGeometry      = RayTracingLOD->bOwnsRayTracingGeometry ? new FRayTracingGeometry ( ) : LODModel.RayTracingGeometry;
//
//		RayTracingLOD->Sections      = &LODModel.Sections;
//		RayTracingLOD->VertexBuffers = &LODModel.VertexBuffers;
//		RayTracingLOD->IndexBuffer   = &LODModel.IndexBuffer;
//		RayTracingLOD->bOwnsBuffers  = false;
//
//		RayTracingLODs.Add ( RayTracingLOD );
//	}
//
//	RayTracingProxy->LODVertexFactories = &LODVertexFactories;
//}


bool FLPPDynamicMeshRenderData::HasValidNaniteData ( ) const
{
	return NaniteResourcesPtr.IsValid ( ) && NaniteResourcesPtr->PageStreamingStates.Num ( ) > 0;
}

bool FLPPDynamicMeshRenderData::HasNaniteFallbackMesh ( EShaderPlatform ShaderPlatform ) const
{
	checkf ( HasValidNaniteData() , TEXT("Should only call HasNaniteFallbackMesh(...) if HasValidNaniteData() returns true.") );

	return bHasNaniteFallbackMesh;
}
