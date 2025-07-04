﻿// Fill out your copyright notice in the Description page of Project Settings.


#include "Subsystem/LPPProceduralWorldSubsystem.h"

void ULPPProceduralWorldSubsystem::Initialize ( FSubsystemCollectionBase& Collection )
{
	Super::Initialize ( Collection );
}

void ULPPProceduralWorldSubsystem::Tick ( float DeltaTime )
{
	Super::Tick ( DeltaTime );

	const int32 PreFrame = FMath::Floor ( 1.0f / PreDeltaTime );
	const int32 JobCount = FMath::Max ( 1 , PreFrame / 10 );

	for ( int32 JobIndex = 0 ; LazyGameThreadJobQueue.IsEmpty ( ) == false && JobIndex < JobCount ; ++JobIndex )
	{
		TFunction < void  ( ) > GameThreadJob;
		LazyGameThreadJobQueue.Dequeue ( GameThreadJob );

		GameThreadJob ( );
	}

	PreDeltaTime = DeltaTime;
}

void ULPPProceduralWorldSubsystem::Deinitialize ( )
{
	Super::Deinitialize ( );

	bIsShuttingDown = true;
	FScopeLock PendingLock ( &PendingJobsLock );
	for ( int32 k = 0 ; k < PendingJobs.Num ( ) ; ++k )
	{
		UE::Tasks::Wait ( { PendingJobs [ k ]->Task } );
	}

	PendingJobs.Empty ( );
	LazyGameThreadJobQueue.Empty ( );
}

TStatId ULPPProceduralWorldSubsystem::GetStatId ( ) const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT ( ULPPMarchingWorldSubsystem , STATGROUP_Tickables );
}

TWeakPtr < FMarchingComputeJob > ULPPProceduralWorldSubsystem::LaunchJob ( const TCHAR* DebugName , const TFunction < void  ( FProgressCancel& Progress , TQueue < TFunction < void  ( ) > , EQueueMode::Mpsc >& GameThreadJob ) >& JobWork )
{
	check ( IsInGameThread ( ) ); // Must In Game Thread

	if ( !ensure ( bIsShuttingDown == false ) )
	{
		return TWeakPtr < FMarchingComputeJob > ( );
	}

	{
		FScopeLock RemovePending ( &PendingJobsLock );
		for ( int32 JobIndex = 0 ; JobIndex < PendingJobs.Num ( ) ; ++JobIndex )
		{
			if ( const TSharedPtr < FMarchingComputeJob >& Job = PendingJobs [ JobIndex ] ; Job->bHasCompleted )
			{
				PendingJobs.RemoveAtSwap ( JobIndex , 1 , EAllowShrinking::No );
				JobIndex -= 1; // rerun the index again to launch the job
			}
		}
	}

	// set up the new job
	TSharedPtr < FMarchingComputeJob > NewJob = MakeShared < FMarchingComputeJob > ( );
	FMarchingComputeJob*               JobPtr = NewJob.Get ( );
	NewJob->Progress                          = MakeUnique < FProgressCancel > ( );
	NewJob->Progress->CancelF                 = [this, JobPtr] ( ) { return bIsShuttingDown || JobPtr->bCancelled; };
	NewJob->DebugName                         = DebugName;
	NewJob->JobWork                           = JobWork;
	NewJob->Task                              = LaunchJobInternal ( NewJob.Get ( ) );

	// add a new job
	FScopeLock                       AddJob ( &PendingJobsLock );
	TWeakPtr < FMarchingComputeJob > ResultData = PendingJobs.Add_GetRef ( MoveTemp ( NewJob ) );

	return ResultData;
}

UE::Tasks::FTask ULPPProceduralWorldSubsystem::LaunchJobInternal ( FMarchingComputeJob* JobPtr )
{
	return UE::Tasks::Launch ( *JobPtr->DebugName ,
	                           [this, JobPtr] ( )
	                           {
		                           JobPtr->JobWork ( *JobPtr->Progress , LazyGameThreadJobQueue );
		                           JobPtr->bHasCompleted = true;
	                           } ,
	                           LowLevelTasks::ETaskPriority::BackgroundHigh );
}
