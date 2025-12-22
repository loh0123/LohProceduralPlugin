// Fill out your copyright notice in the Description page of Project Settings.


#include "Subsystem/LPPProceduralWorldSubsystem.h"

void ULPPProceduralWorldSubsystem::Initialize ( FSubsystemCollectionBase& Collection )
{
	Super::Initialize ( Collection );

	LastTickTime = 0.0f;
	TickInterval = 1.0f / 60.0f;
}

void ULPPProceduralWorldSubsystem::Tick ( float DeltaTime )
{
	Super::Tick ( DeltaTime );

	const int32 JobCount = FMath::FloorToInt ( LastTickTime / TickInterval );

	LastTickTime += DeltaTime;
	LastTickTime -= TickInterval * JobCount;

	for ( int32 JobIndex = 0 ; LazyGameThreadJobQueue.IsEmpty ( ) == false && JobIndex < JobCount ; ++JobIndex )
	{
		( *LazyGameThreadJobQueue.Peek ( ) ) ( );
		LazyGameThreadJobQueue.Pop ( );
	}
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
	RETURN_QUICK_DECLARE_CYCLE_STAT ( ULPPChunkManagerSubsystem , STATGROUP_Tickables );
}

TWeakPtr < FProceduralWorldComputeJob > ULPPProceduralWorldSubsystem::LaunchJob ( const TCHAR* DebugName , const TFunction < void  ( FProgressCancel& Progress , TQueue < TFunction < void  ( ) > , EQueueMode::Mpsc >& GameThreadJob ) >& JobWork , const LowLevelTasks::ETaskPriority Priority , const bool bSingleThreadMode )
{
	check ( IsInGameThread ( ) ); // Must In Game Thread

	if ( !ensure ( bIsShuttingDown == false ) )
	{
		return TWeakPtr < FProceduralWorldComputeJob > ( );
	}

	{
		FScopeLock RemovePending ( &PendingJobsLock );
		for ( int32 JobIndex = 0 ; JobIndex < PendingJobs.Num ( ) ; ++JobIndex )
		{
			if ( const TSharedPtr < FProceduralWorldComputeJob >& Job = PendingJobs [ JobIndex ] ; Job->bHasCompleted )
			{
				PendingJobs.RemoveAtSwap ( JobIndex , 1 , EAllowShrinking::No );
				JobIndex -= 1; // rerun the index again to launch the job
			}
		}
	}

	// set up the new job
	TSharedPtr < FProceduralWorldComputeJob > NewJob = MakeShared < FProceduralWorldComputeJob > ( );
	FProceduralWorldComputeJob*               JobPtr = NewJob.Get ( );
	NewJob->Progress                                 = MakeUnique < FProgressCancel > ( );
	NewJob->Progress->CancelF                        = [this, JobPtr] ( ) { return bIsShuttingDown || JobPtr->bCancelled; };
	NewJob->DebugName                                = DebugName;
	NewJob->JobWork                                  = JobWork;

	if ( bSingleThreadMode )
	{
		NewJob->JobWork ( *JobPtr->Progress , LazyGameThreadJobQueue );
		NewJob->bHasCompleted = true;
	}
	else
	{
		NewJob->Task = LaunchJobInternal ( NewJob.Get ( ) , Priority );
	}

	// add a new job
	FScopeLock AddJob ( &PendingJobsLock );

	TWeakPtr < FProceduralWorldComputeJob > ResultData = PendingJobs.Add_GetRef ( MoveTemp ( NewJob ) );

	return ResultData;
}

UE::Tasks::FTask ULPPProceduralWorldSubsystem::LaunchJobInternal ( FProceduralWorldComputeJob* JobPtr , const LowLevelTasks::ETaskPriority Priority )
{
	return UE::Tasks::Launch ( *JobPtr->DebugName ,
	                           [this, JobPtr] ( )
	                           {
		                           JobPtr->JobWork ( *JobPtr->Progress , LazyGameThreadJobQueue );
		                           JobPtr->bHasCompleted = true;
	                           } ,
	                           Priority );
}
