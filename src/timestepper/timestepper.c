#include "timestepper.h"

/* Initialize a {\tt timeStepper} struct
 *
 * 1) 
 *
 * @param Input: ts, an uninitializied {\tt timeStepper} struct
 * @param Output: ts, an initialized {\tt timeStepper} struct
 */
void timeStepperInit(struct timeStepper ts[ARRAY_ARGS 1])
{
  /* If explicit or imex time stepping, then use a DM with no ghost zones. Graph
   * coloring with ensure that the number of evaluations to construct the
   * jacobian will be equal to DOF^2 */
  #if (TIME_STEPPING==EXPLICIT || TIME_STEPPING==IMEX)
    initGridData(DOF, 0, &ts->primNPlusOne);
  #elif (TIME_STEPPING==IMPLICIT)
    initGridData(DOF, NG, &ts->primNPlusOne);
  #endif

  initGridData(DOF, NG, &ts->primNPlusHalf);
  initGridData(DOF, NG, &ts->primN);

  initGridData(DOF, 0, &ts->conservedVarsN);
  initGridData(DOF, 0, &ts->divFluxes);
  initGridData(DOF, 0, &ts->sources);

  initGridData(DOF, 0, &ts->residual);

  initGridData(COMPUTE_DIM, 0, &ts->dtGrid);
  initGridData(64, 0, &ts->connection);

  SNESCreate(PETSC_COMM_WORLD, &ts->snes);
  SNESSetDM(ts->snes, ts->primNPlusOne.dm);
  SNESSetFunction(ts->snes, ts->residual.vec, computeResidual, ts);
  SNESSetFromOptions(ts->snes);

  ts->dt = DT;
  ts->t = START_TIME;
  ts->tDump = START_TIME;

  ts->timeStepCounter = 0;
  ts->dumpCounter     = 0;

  ts->isZerothIterationOfSNES = 0;

  ts->computeDivOfFluxAtN         = 0;
  ts->computeDivOfFluxAtNPlusHalf = 0;
  ts->computeSourcesAtN           = 0;
  ts->computeSourcesAtNPlusHalf   = 0;

  /* Initialize problem dependent data */
  PetscMalloc1(1, &ts->problemSpecificData);

  if (ts->primN.iLocalSize % TILE_SIZE_X1 != 0)
  {
    PetscPrintf(PETSC_COMM_WORLD,
                "TILE_SIZE_X1 = %d does not divide X1Size = %d exactly\n",
                TILE_SIZE_X1, ts->primN.iLocalSize
               );
    MPI_Abort(PETSC_COMM_WORLD, 1);
  }
  #if (COMPUTE_DIM==2 || COMPUTE_DIM==3)
    if (ts->primN.jLocalSize % TILE_SIZE_X2 != 0)
    {
      PetscPrintf(PETSC_COMM_WORLD,
                  "TILE_SIZE_X2 = %d does not divide X2Size = %d exactly\n",
                  TILE_SIZE_X2, ts->primN.jLocalSize
                 );
      MPI_Abort(PETSC_COMM_WORLD, 1);
    }
  #endif

  PetscPrintf(PETSC_COMM_WORLD, "\n");
  PetscPrintf(PETSC_COMM_WORLD,
              "#################################################\n");
  PetscPrintf(PETSC_COMM_WORLD,
              "           Memory allocation complete\n\n");

  int numProcs;
  MPI_Comm_size(PETSC_COMM_WORLD, &numProcs);
  PetscPrintf(PETSC_COMM_WORLD,
              " Number of MPI procs being used       = %d\n",
              numProcs
             );
  #if (USE_OPENMP)
    PetscPrintf(PETSC_COMM_WORLD,
                " Number of OpenMP threads being used  = %d\n",
                omp_get_max_threads()
               );
  #endif
  #if (COMPUTE_DIM==1)
    PetscPrintf(PETSC_COMM_WORLD,
                " Grid size                            = %d\n",
                N1
               );
    PetscPrintf(PETSC_COMM_WORLD,
                " Grid points in each MPI process      = %d\n",
                ts->primN.iLocalSize
               );
    PetscPrintf(PETSC_COMM_WORLD,
                " Grid points in each tile             = %d\n",
                TILE_SIZE_X1
               );
    PetscPrintf(PETSC_COMM_WORLD,
                " Number of tiles in each MPI process  = %d\n",
                ts->primN.iLocalSize/TILE_SIZE_X1
               );
    PetscPrintf(PETSC_COMM_WORLD,
                " Number of tiles in each OpenMP thread= %d\n",
                ts->primN.iLocalSize/TILE_SIZE_X1/omp_get_max_threads()
               );
  #elif (COMPUTE_DIM==2)
    PetscPrintf(PETSC_COMM_WORLD,
                " Grid size                            = %d x %d\n",
                N1, N2
               );
    PetscPrintf(PETSC_COMM_WORLD,
                " Grid points in each MPI process      = %d x %d\n",
                ts->primN.iLocalSize, ts->primN.jLocalSize
               );
    PetscPrintf(PETSC_COMM_WORLD,
                " Grid points in each tile             = %d x %d\n",
                TILE_SIZE_X1, TILE_SIZE_X2
               );
    PetscPrintf(PETSC_COMM_WORLD,
                " Number of tiles in each MPI process  = %d x %d\n",
                ts->primN.iLocalSize/TILE_SIZE_X1,
                ts->primN.jLocalSize/TILE_SIZE_X2
               );
    PetscPrintf(PETSC_COMM_WORLD,
                " Number of tiles in each OpenMP thread= %d x %d\n",
                ts->primN.iLocalSize/TILE_SIZE_X1/omp_get_max_threads(),
                ts->primN.jLocalSize/TILE_SIZE_X2/omp_get_max_threads()
               );
  #elif (COMPUTE_DIM==3)
    PetscPrintf(PETSC_COMM_WORLD,
                " Grid size                            = %d x %d x %d\n",
                N1, N2, N3
               );
    PetscPrintf(PETSC_COMM_WORLD,
                " Grid points in each MPI process      = %d x %d x %d\n",
                ts->primN.iLocalSize, ts->primN.jLocalSize, 
                ts->primN.kLocalSize
               );
    PetscPrintf(PETSC_COMM_WORLD,
                " Grid points in each tile             = %d x %d x %d\n",
                TILE_SIZE_X1, TILE_SIZE_X2, 2*NG + 1
               );
    PetscPrintf(PETSC_COMM_WORLD,
                " Number of tiles in each MPI process  = %d x %d\n",
                ts->primN.iLocalSize/TILE_SIZE_X1,
                ts->primN.jLocalSize/TILE_SIZE_X2
               );
    PetscPrintf(PETSC_COMM_WORLD,
                " Number of tiles in each OpenMP thread= %d x %d\n",
                ts->primN.iLocalSize/TILE_SIZE_X1/omp_get_max_threads(),
                ts->primN.jLocalSize/TILE_SIZE_X2/omp_get_max_threads()
               );
  #endif

  PetscPrintf(PETSC_COMM_WORLD,
              "#################################################\n");
  PetscPrintf(PETSC_COMM_WORLD, "\n");

//  /* Precompute the Chritoffel symbols gammaUpDownDown */
//  PetscPrintf(PETSC_COMM_WORLD, "Computing Christoffel symbols...");
//  setChristoffelSymbols(ts);
//  PetscPrintf(PETSC_COMM_WORLD, "done\n");
//
//  #if (RESTART)
//    PetscMPIInt rank;
//    MPI_Comm_rank(PETSC_COMM_WORLD,&rank);
//
//    if (rank==0)
//    {
//      if (access(RESTART_FILE, F_OK) != -1)
//      {
//        /* File exists */
//        PetscPrintf(PETSC_COMM_WORLD, "\nFound restart file: %s\n\n", RESTART_FILE); 
//      }
//      else
//      {
//        /* File does not exist */
//        PetscPrintf(PETSC_COMM_WORLD, "\n");
//        //SETERRQ1(PETSC_COMM_WORLD, 1, "Restart file %s does not exist\n",
//        //         RESTART_FILE);
//      }
//    }
//
//    PetscViewer viewer;
//    PetscViewerHDF5Open(PETSC_COMM_WORLD, "restartfile.h5",
//                        FILE_MODE_READ, &viewer);
//    PetscObjectSetName((PetscObject) ts->primPetscVecOld, "primVars");
//    VecLoad(ts->primPetscVecOld, viewer);
//    PetscViewerDestroy(&viewer);
//
//  #else
//
//    /* Set initialConditions from problem */
//    initialConditions(ts);
//
//  #endif /* RESTART option */
//
//  /* Output the initial conditions */
//  VecCopy(ts->primPetscVecOld, ts->primPetscVec);
//  diagnostics(ts);

}

//void timeStepperInit(struct timeStepper ts[ARRAY_ARGS 1])
//{
//  SNESCreate(PETSC_COMM_WORLD, &ts->snes);
//
//  /* Periodic boundary conditions handled by Petsc since it is a global boundary
//   * condition. Here we check for the boundary at the left edge. Obviously the
//   * boundary at the right edge also must be PERIODIC if left edge is PERIODIC */
//  #if (COMPUTE_DIM==1)
//
//    #if (PHYSICAL_BOUNDARY_LEFT_EDGE==PERIODIC)
//      DMDACreate1d(PETSC_COMM_WORLD, DM_BOUNDARY_PERIODIC, N1, DOF, NG, NULL,
//                   &ts->dmdaWithGhostZones);
//    #else
//      DMDACreate1d(PETSC_COMM_WORLD, DM_BOUNDARY_GHOSTED, N1, DOF, NG, NULL,
//                   &ts->dmdaWithGhostZones);
//    #endif
//  
//  #elif (COMPUTE_DIM==2)
//
//      #if (PHYSICAL_BOUNDARY_TOP_EDGE==PERIODIC && PHYSICAL_BOUNDARY_LEFT_EDGE==PERIODIC)
//        DMDACreate2d(PETSC_COMM_WORLD, 
//                     DM_BOUNDARY_PERIODIC, DM_BOUNDARY_PERIODIC,
//                     DMDA_STENCIL_BOX,
//                     N1, N2,
//                     PETSC_DECIDE, PETSC_DECIDE,
//                     DOF, NG, PETSC_NULL, PETSC_NULL, &ts->dmdaWithGhostZones);
//
//      #elif (PHYSICAL_BOUNDARY_LEFT_EDGE==PERIODIC)
//        DMDACreate2d(PETSC_COMM_WORLD, 
//                     DM_BOUNDARY_PERIODIC, DM_BOUNDARY_GHOSTED,
//                     DMDA_STENCIL_BOX,
//                     N1, N2,
//                     PETSC_DECIDE, PETSC_DECIDE,
//                     DOF, NG, PETSC_NULL, PETSC_NULL, &ts->dmdaWithGhostZones);
//
//      #elif (PHYSICAL_BOUNDARY_TOP_EDGE==PERIODIC)
//        DMDACreate2d(PETSC_COMM_WORLD, 
//                     DM_BOUNDARY_GHOSTED, DM_BOUNDARY_PERIODIC,
//                     DMDA_STENCIL_BOX,
//                     N1, N2,
//                     PETSC_DECIDE, PETSC_DECIDE,
//                     DOF, NG, PETSC_NULL, PETSC_NULL, &ts->dmdaWithGhostZones);
//      #else
//        DMDACreate2d(PETSC_COMM_WORLD, 
//                     DM_BOUNDARY_GHOSTED, DM_BOUNDARY_GHOSTED,
//                     DMDA_STENCIL_BOX,
//                     N1, N2,
//                     PETSC_DECIDE, PETSC_DECIDE,
//                     DOF, NG, PETSC_NULL, PETSC_NULL, &ts->dmdaWithGhostZones);
//      #endif
//
//  #endif /* Choose dim and create dmdaWithGhostZones */
//
//  /* Now create dmdaWithoutGhostZones for the vectors that don't need
//   * communication */
//  #if (COMPUTE_DIM==1)
//    DMDACreate1d(PETSC_COMM_WORLD, DM_BOUNDARY_NONE, N1, DOF, 0, NULL,
//                 &ts->dmdaWithoutGhostZones);
//  #elif (COMPUTE_DIM==2)
//    DMDACreate2d(PETSC_COMM_WORLD, 
//                 DM_BOUNDARY_NONE, DM_BOUNDARY_NONE,
//                 DMDA_STENCIL_BOX,
//                 N1, N2,
//                 PETSC_DECIDE, PETSC_DECIDE,
//                 DOF, 0, PETSC_NULL, PETSC_NULL, &ts->dmdaWithoutGhostZones);
//  #endif /* Choose dimension */
//
//  /* If explicit or imex time stepping, then create another DM with no ghost
//   * zones. Graph coloring with ensure that the number of evaluations to construct
//   * the jacobian will be equal to DOF^2 */
//  #if (TIME_STEPPING==EXPLICIT || TIME_STEPPING==IMEX)
//    SNESSetDM(ts->snes, ts->dmdaWithoutGhostZones);
//  #elif (TIME_STEPPING==IMPLICIT)
//  /* If implicit time stepping, then use the DM created earlier which has ghost
//   * zones, in SNES */
//    SNESSetDM(ts->snes, ts->dmdaWithGhostZones);
//  #endif
//
//  DMDAGetCorners(ts->dmdaWithGhostZones,
//                 &ts->X1Start, &ts->X2Start, &ts->X3Start,
//                 &ts->X1Size, &ts->X2Size, &ts->X3Size);
//  
//  DMCreateGlobalVector(ts->dmdaWithGhostZones, &ts->primPetscVecOld);
//  DMCreateGlobalVector(ts->dmdaWithGhostZones, &ts->primPetscVecLastStep);
//  DMCreateGlobalVector(ts->dmdaWithGhostZones, &ts->primPetscVecLambda);
//  DMCreateGlobalVector(ts->dmdaWithGhostZones, &ts->primPetscVecHalfStep);
//  DMCreateGlobalVector(ts->dmdaWithoutGhostZones, &ts->divFluxPetscVecOld);
//  DMCreateGlobalVector(ts->dmdaWithoutGhostZones, &ts->conservedVarsPetscVecOld);
//  DMCreateGlobalVector(ts->dmdaWithoutGhostZones, &ts->sourceTermsPetscVecOld);
//  DMCreateGlobalVector(ts->dmdaWithoutGhostZones, &ts->residualPetscVec);
//  DMCreateGlobalVector(ts->dmdaWithoutGhostZones, &ts->OldresidualPetscVec);
//  DMCreateGlobalVector(ts->dmdaWithoutGhostZones, &ts->LambdaresidualPetscVec);
//  DMCreateGlobalVector(ts->dmdaWithoutGhostZones, &ts->LastStepresidualPetscVec);
//
//  #if (TIME_STEPPING==EXPLICIT || TIME_STEPPING==IMEX)
//    DMCreateGlobalVector(ts->dmdaWithoutGhostZones, &ts->primPetscVec);
//  #elif (TIME_STEPPING==IMPLICIT)
//    DMCreateGlobalVector(ts->dmdaWithGhostZones, &ts->primPetscVec);
//  #endif
//
//  VecSet(ts->primPetscVecOld, 0.);
//  VecSet(ts->primPetscVecLastStep, 0.);
//  VecSet(ts->primPetscVecLambda, 0.);
//  VecSet(ts->primPetscVecHalfStep, 0.);
//  VecSet(ts->divFluxPetscVecOld, 0.);
//  VecSet(ts->conservedVarsPetscVecOld, 0.);
//  VecSet(ts->sourceTermsPetscVecOld, 0.);
//  VecSet(ts->residualPetscVec, 0.);
//  VecSet(ts->OldresidualPetscVec, 0.);
//  VecSet(ts->LambdaresidualPetscVec, 0.);
//  VecSet(ts->LastStepresidualPetscVec, 0.);
//  VecSet(ts->primPetscVec, 0.);
//
//  SNESSetFunction(ts->snes, ts->residualPetscVec,
//                  computeResidual, ts);
//
//  SNESSetFromOptions(ts->snes);
//
//  ts->dt = DT;
//  ts->t = START_TIME;
//  ts->tDump = START_TIME;
//
//  ts->timeStepCounter = 0;
//  ts->dumpCounter = 0;
//
//  ts->computeOldSourceTermsAndOldDivOfFluxes = 0;
//  ts->computeDivOfFluxAtTimeN = 0;
//  ts->computeDivOfFluxAtTimeNPlusHalf = 0;
//  ts->computeSourceTermsAtTimeN = 0;
//  ts->computeSourceTermsAtTimeNPlusHalf = 0;
//
//  /* Now create dmda for the connection coefficients */
//  #if (COMPUTE_DIM==1)
//    DMDACreate1d(PETSC_COMM_WORLD, DM_BOUNDARY_NONE, N1, 64, 0, NULL,
//                 &ts->connectionDMDA);
//  #elif (COMPUTE_DIM==2)
//    DMDACreate2d(PETSC_COMM_WORLD, DM_BOUNDARY_NONE, DM_BOUNDARY_NONE,
//                 DMDA_STENCIL_BOX, N1, N2, PETSC_DECIDE, PETSC_DECIDE,
//                 64, 0, PETSC_NULL, PETSC_NULL, &ts->connectionDMDA);
//  #endif /* Choose dimension */
//
//  DMCreateGlobalVector(ts->connectionDMDA, &ts->connectionPetscVec);
//
//  /* Now create dmda for dt */
//  #if (COMPUTE_DIM==1)
//    DMDACreate1d(PETSC_COMM_WORLD, DM_BOUNDARY_NONE, N1, COMPUTE_DIM, 0, NULL,
//                 &ts->dmdaDt);
//  #elif (COMPUTE_DIM==2)
//    DMDACreate2d(PETSC_COMM_WORLD, 
//                 DM_BOUNDARY_NONE, DM_BOUNDARY_NONE,
//                 DMDA_STENCIL_BOX,
//                 N1, N2,
//                 PETSC_DECIDE, PETSC_DECIDE,
//                 COMPUTE_DIM, 0, PETSC_NULL, PETSC_NULL,
//                 &ts->dmdaDt);
//  #endif /* Choose dimension */
//
//  DMCreateGlobalVector(ts->dmdaDt, &ts->dtPetscVec);
//
//  #if (CONDUCTION)
//  initConductionDataStructures(ts);
//  #endif
//
//  #if (VISCOSITY)
//  initViscosityDataStructures(ts);
//  #endif
//
//
//  /* Initialize problem dependent data */
//  PetscMalloc1(1, &ts->problemSpecificData);
//
//  if (ts->X1Size % TILE_SIZE_X1 != 0)
//  {
//    SETERRQ2(PETSC_COMM_WORLD, 1,
//             "TILE_SIZE_X1 = %d does not divide X1Size = %d exactly\n",
//             TILE_SIZE_X1, ts->X1Size);
//  }
//  #if (COMPUTE_DIM==2)
//    if (ts->X2Size % TILE_SIZE_X2 != 0)
//    {
//      SETERRQ2(PETSC_COMM_WORLD, 1,
//               "TILE_SIZE_X2 = %d does not divide X2Size = %d exactly\n",
//               TILE_SIZE_X2, ts->X2Size);
//    }
//  #endif
//
//  PetscPrintf(PETSC_COMM_WORLD, "\n");
//  PetscPrintf(PETSC_COMM_WORLD,
//              "#################################################\n");
//  PetscPrintf(PETSC_COMM_WORLD,
//              "           Memory allocation complete\n\n");
//
//  int numProcs;
//  MPI_Comm_size(PETSC_COMM_WORLD, &numProcs);
//  PetscPrintf(PETSC_COMM_WORLD,
//              " Number of MPI procs being used       = %d\n",
//              numProcs);
//  #if (COMPUTE_DIM==1)
//    PetscPrintf(PETSC_COMM_WORLD,
//                " Grid size                            = %d\n",
//                N1);
//    PetscPrintf(PETSC_COMM_WORLD,
//                " Grid points in each MPI process      = %d\n",
//                ts->X1Size);
//    PetscPrintf(PETSC_COMM_WORLD,
//                " Grid points in each tile             = %d\n",
//                TILE_SIZE_X1);
//    PetscPrintf(PETSC_COMM_WORLD,
//                " Number of tiles in each MPI process  = %d\n",
//                ts->X1Size/TILE_SIZE_X1);
//  #elif (COMPUTE_DIM==2)
//    PetscPrintf(PETSC_COMM_WORLD,
//                " Grid size                            = %d x %d\n",
//                N1, N2);
//    PetscPrintf(PETSC_COMM_WORLD,
//                " Grid points in each MPI process      = %d x %d\n",
//                ts->X1Size, ts->X2Size);
//    PetscPrintf(PETSC_COMM_WORLD,
//                " Grid points in each tile             = %d x %d\n",
//                TILE_SIZE_X1, TILE_SIZE_X2);
//    PetscPrintf(PETSC_COMM_WORLD,
//                " Number of tiles in each MPI process  = %d x %d\n",
//                ts->X1Size/TILE_SIZE_X1, ts->X2Size/TILE_SIZE_X2);
//  #endif
//
//  PetscPrintf(PETSC_COMM_WORLD,
//              "#################################################\n");
//  PetscPrintf(PETSC_COMM_WORLD, "\n");
//
//  /* Precompute the Chritoffel symbols gammaUpDownDown */
//  PetscPrintf(PETSC_COMM_WORLD, "Computing Christoffel symbols...");
//  setChristoffelSymbols(ts);
//  PetscPrintf(PETSC_COMM_WORLD, "done\n");
//
//  #if (RESTART)
//    PetscMPIInt rank;
//    MPI_Comm_rank(PETSC_COMM_WORLD,&rank);
//
//    if (rank==0)
//    {
//      if (access(RESTART_FILE, F_OK) != -1)
//      {
//        /* File exists */
//        PetscPrintf(PETSC_COMM_WORLD, "\nFound restart file: %s\n\n", RESTART_FILE); 
//      }
//      else
//      {
//        /* File does not exist */
//        PetscPrintf(PETSC_COMM_WORLD, "\n");
//        //SETERRQ1(PETSC_COMM_WORLD, 1, "Restart file %s does not exist\n",
//        //         RESTART_FILE);
//      }
//    }
//
//    PetscViewer viewer;
//    PetscViewerHDF5Open(PETSC_COMM_WORLD, "restartfile.h5",
//                        FILE_MODE_READ, &viewer);
//    PetscObjectSetName((PetscObject) ts->primPetscVecOld, "primVars");
//    VecLoad(ts->primPetscVecOld, viewer);
//    PetscViewerDestroy(&viewer);
//
//  #else
//
//    /* Set initialConditions from problem */
//    initialConditions(ts);
//
//  #endif /* RESTART option */
//
//  /* Output the initial conditions */
//  VecCopy(ts->primPetscVecOld, ts->primPetscVec);
//  diagnostics(ts);
//
//}
//
//#if (CONDUCTION)
//void initConductionDataStructures(struct timeStepper ts[ARRAY_ARGS 1])
//{
//
//  #if (COMPUTE_DIM==1)
//    DMDACreate1d(PETSC_COMM_WORLD, DM_BOUNDARY_NONE, N1, COMPUTE_DIM, 0, NULL,
//                 &ts->gradTDM);
//
//    DMDACreate1d(PETSC_COMM_WORLD, DM_BOUNDARY_NONE, N1, COMPUTE_DIM*NDIM, 0, NULL,
//                 &ts->graduConDM);
//
//    DMDACreate1d(PETSC_COMM_WORLD, DM_BOUNDARY_NONE, N1, COMPUTE_DIM, 0, NULL,
//                 &ts->graduConHigherOrderTermsDM);
//
//  #elif (COMPUTE_DIM==2)
//    DMDACreate2d(PETSC_COMM_WORLD, 
//                 DM_BOUNDARY_NONE, DM_BOUNDARY_NONE,
//                 DMDA_STENCIL_BOX,
//                 N1, N2,
//                 PETSC_DECIDE, PETSC_DECIDE,
//                 COMPUTE_DIM, 0, PETSC_NULL, PETSC_NULL,
//                 &ts->gradTDM);
//
//    DMDACreate2d(PETSC_COMM_WORLD, 
//                 DM_BOUNDARY_NONE, DM_BOUNDARY_NONE,
//                 DMDA_STENCIL_BOX,
//                 N1, N2,
//                 PETSC_DECIDE, PETSC_DECIDE,
//                 COMPUTE_DIM*NDIM, 0, PETSC_NULL, PETSC_NULL,
//                 &ts->graduConDM);
//
//    DMDACreate2d(PETSC_COMM_WORLD, 
//                 DM_BOUNDARY_NONE, DM_BOUNDARY_NONE,
//                 DMDA_STENCIL_BOX,
//                 N1, N2,
//                 PETSC_DECIDE, PETSC_DECIDE,
//                 COMPUTE_DIM, 0, PETSC_NULL, PETSC_NULL,
//                 &ts->graduConHigherOrderTermsDM);
//
//  #endif
//
//  DMCreateGlobalVector(ts->gradTDM, &ts->gradTPetscVec);
//  DMCreateGlobalVector(ts->graduConDM, &ts->graduConPetscVec);
//  DMCreateGlobalVector(ts->graduConHigherOrderTermsDM, 
//                       &ts->graduConHigherOrderTerm1PetscVec);
//  DMCreateGlobalVector(ts->graduConHigherOrderTermsDM, 
//                       &ts->graduConHigherOrderTerm2PetscVec);
//
//  VecSet(ts->gradTPetscVec, 0.);
//  VecSet(ts->graduConPetscVec, 0.);
//  VecSet(ts->graduConHigherOrderTerm1PetscVec, 0.);
//  VecSet(ts->graduConHigherOrderTerm2PetscVec, 0.);
//}
//
//void destroyConductionDataStructures(struct timeStepper ts[ARRAY_ARGS 1])
//{
//  VecDestroy(&ts->gradTPetscVec);
//  VecDestroy(&ts->graduConPetscVec);
//  VecDestroy(&ts->graduConHigherOrderTerm1PetscVec);
//  VecDestroy(&ts->graduConHigherOrderTerm2PetscVec);
//
//  DMDestroy(&ts->gradTDM);
//  DMDestroy(&ts->graduConDM);
//  DMDestroy(&ts->graduConHigherOrderTermsDM);
//}
//#endif
//
//
//#if (VISCOSITY)
//void initViscosityDataStructures(struct timeStepper ts[ARRAY_ARGS 1])
//{
//
//  #if (COMPUTE_DIM==1)
//    DMDACreate1d(PETSC_COMM_WORLD, DM_BOUNDARY_NONE, N1, COMPUTE_DIM*NDIM, 0, NULL,
//                 &ts->graduConVisDM);
//    DMDACreate1d(PETSC_COMM_WORLD, DM_BOUNDARY_NONE, N1, COMPUTE_DIM, 0, NULL,
//                 &ts->graduConHigherOrderTermsVisDM);
//
//  #elif (COMPUTE_DIM==2)
//    DMDACreate2d(PETSC_COMM_WORLD, 
//                   DM_BOUNDARY_NONE, DM_BOUNDARY_NONE,
//                   DMDA_STENCIL_BOX,
//                   N1, N2,
//                   PETSC_DECIDE, PETSC_DECIDE,
//                   COMPUTE_DIM*NDIM, 0, PETSC_NULL, PETSC_NULL,
//                   &ts->graduConVisDM);
//    DMDACreate2d(PETSC_COMM_WORLD, 
//                 DM_BOUNDARY_NONE, DM_BOUNDARY_NONE,
//                 DMDA_STENCIL_BOX,
//                 N1, N2,
//                 PETSC_DECIDE, PETSC_DECIDE,
//                 COMPUTE_DIM, 0, PETSC_NULL, PETSC_NULL,
//                 &ts->graduConHigherOrderTermsVisDM);
//
//  #endif
//
//  DMCreateGlobalVector(ts->graduConVisDM, &ts->graduConVisPetscVec);
//  DMCreateGlobalVector(ts->graduConHigherOrderTermsVisDM, 
//                       &ts->graduConHigherOrderTerm1VisPetscVec);
//  DMCreateGlobalVector(ts->graduConHigherOrderTermsVisDM, 
//                       &ts->graduConHigherOrderTerm2VisPetscVec);
//
//  VecSet(ts->graduConVisPetscVec, 0.);
//  VecSet(ts->graduConHigherOrderTerm1VisPetscVec, 0.);
//  VecSet(ts->graduConHigherOrderTerm2VisPetscVec, 0.);
//}
//
//void destroyViscosityDataStructures(struct timeStepper ts[ARRAY_ARGS 1])
//{
//  VecDestroy(&ts->graduConVisPetscVec);
//  VecDestroy(&ts->graduConHigherOrderTerm1VisPetscVec);
//  VecDestroy(&ts->graduConHigherOrderTerm2VisPetscVec);
//
//  DMDestroy(&ts->graduConVisDM);
//  DMDestroy(&ts->graduConHigherOrderTermsVisDM);
//}
//#endif
//
//void setChristoffelSymbols(struct timeStepper ts[ARRAY_ARGS 1])
//{
//  VecSet(ts->connectionPetscVec, 0.);
//
//  ARRAY(connectionGlobal);
//  DMDAVecGetArrayDOF(ts->connectionDMDA, ts->connectionPetscVec,
//                     &connectionGlobal);
//
//  #if (USE_OPENMP)
//    #pragma omp parallel for
//  #endif
//  LOOP_OVER_TILES(ts->X1Size, ts->X2Size)
//  {
//    LOOP_INSIDE_TILE(0, TILE_SIZE_X1, 0, TILE_SIZE_X2)
//    {
//      
//      struct gridZone zone;
//      setGridZone(iTile, jTile,
//                  iInTile, jInTile,
//                  ts->X1Start, ts->X2Start,
//                  ts->X1Size, ts->X2Size,
//                  &zone);
//
//      REAL XCoords[NDIM];
//      getXCoords(&zone, CENTER, XCoords);
//      struct geometry geom; setGeometry(XCoords, &geom);
//
//      /* Now compute connectionGlobal with 
//       * Index Up   - eta
//       * Index down - mu
//       * Index down - nu */
//      for (int eta=0; eta<NDIM; eta++)
//      {
//        for (int mu=0; mu<NDIM; mu++)
//        {
//          for (int nu=0; nu<NDIM; nu++)
//          {
//            for (int alpha=0; alpha<NDIM; alpha++)
//            {
//              #if (COMPUTE_DIM==1)
//                connectionGlobal[zone.i][GAMMA_UP_DOWN_DOWN(eta, mu, nu)]
//              #elif (COMPUTE_DIM==2)
//                connectionGlobal[zone.j][zone.i][GAMMA_UP_DOWN_DOWN(eta, mu, nu)]
//              #endif
//              +=
//                geom.gCon[eta][alpha]
//              * gammaDownDownDown(alpha, mu, nu, XCoords);
//            }
//          }
//        }
//      }
//
//    }
//  }
//
//  DMDAVecRestoreArrayDOF(ts->connectionDMDA, ts->connectionPetscVec,
//                         &connectionGlobal);
//}
//
//void timeStepperDestroy(struct timeStepper ts[ARRAY_ARGS 1])
//{
//  VecDestroy(&ts->primPetscVecOld);
//  VecDestroy(&ts->primPetscVecLastStep);
//  VecDestroy(&ts->primPetscVecLambda);
//  VecDestroy(&ts->divFluxPetscVecOld);
//  VecDestroy(&ts->conservedVarsPetscVecOld);
//  VecDestroy(&ts->sourceTermsPetscVecOld);
//  VecDestroy(&ts->residualPetscVec);
//  VecDestroy(&ts->OldresidualPetscVec);
//  VecDestroy(&ts->LastStepresidualPetscVec);
//  VecDestroy(&ts->LambdaresidualPetscVec);
//  VecDestroy(&ts->primPetscVec);
//  VecDestroy(&ts->connectionPetscVec);
//
//  DMDestroy(&ts->dmdaWithGhostZones);
//  DMDestroy(&ts->dmdaWithoutGhostZones);
//  DMDestroy(&ts->connectionDMDA);
//
//  SNESDestroy(&ts->snes);
//
//  PetscFree(ts->problemSpecificData);
//
//  #if (CONDUCTION)
//    destroyConductionDataStructures(ts);
//  #endif
//
//  #if (VISCOSITY)
//    destroyViscosityDataStructures(ts);
//  #endif
//
//  PetscPrintf(PETSC_COMM_WORLD, "\n");
//  PetscPrintf(PETSC_COMM_WORLD, "################################\n");
//  PetscPrintf(PETSC_COMM_WORLD, "# Memory deallocation complete #\n");
//  PetscPrintf(PETSC_COMM_WORLD, "################################\n");
//  PetscPrintf(PETSC_COMM_WORLD, "\n");
//}
