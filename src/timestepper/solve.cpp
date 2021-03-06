#include "timestepper.hpp"

void timeStepper::solve(grid &primGuess)
{
  int world_rank;
  MPI_Comm_rank(PETSC_COMM_WORLD, &world_rank);
  int world_size;
  MPI_Comm_size(PETSC_COMM_WORLD, &world_size);

  for (int nonLinearIter=0;
       nonLinearIter < params::maxNonLinearIter; nonLinearIter++
      )
  {
    af::timer jacobianAssemblyTimer = af::timer::start();
    int numReadsResidual, numWritesResidual;
    computeResidual(primGuess, *residual,
                    numReadsResidual, numWritesResidual
                   );
    for (int var=0; var < vars::numFluidVars; var++)
    {
      /* Need residualSoA to compute norms */
      residualSoA(span, span, span, var) = residual->vars[var];

      /* Initialize primGuessPlusEps. Needed to numerically assemble the
       * Jacobian */
      primGuessPlusEps->vars[var]        = primGuess.vars[var];
    }

    /* Sum along last dim:vars to get L2 norm */
    array l2Norm  = 
      af::sum(af::pow(residualSoA(domainX1, domainX2, domainX3), 2.), 3);
    l2Norm.eval();
    array notConverged      = l2Norm > params::nonlinearsolve_atol;
    array conditionIndices  = where(notConverged > 0);
    int localNonConverged = conditionIndices.elements();

    /* Communicate residual */
    double localresnorm = 
      af::norm(af::flat(residualSoA(domainX1, domainX2, domainX3)),
               AF_NORM_VECTOR_1
              );
    double globalresnorm = localresnorm;
    int globalNonConverged = localNonConverged;
    if (world_rank == 0)
    {
	    double temp;
	    int Nel;
	    for(int i=1;i<world_size;i++)
	    {
	      MPI_Recv(&temp, 1, MPI_DOUBLE, i, i, PETSC_COMM_WORLD,MPI_STATUS_IGNORE);
	      MPI_Recv(&Nel, 1, MPI_INT, i, i+world_size, PETSC_COMM_WORLD,MPI_STATUS_IGNORE);
	      globalresnorm+=temp;
	      globalNonConverged+=Nel;
	    }
    }
    else
    {
	    MPI_Send(&localresnorm, 1, MPI_DOUBLE, 0, world_rank, PETSC_COMM_WORLD);
	    MPI_Send(&localNonConverged, 1, MPI_INT, 0, world_rank+world_size, PETSC_COMM_WORLD);
    }
    MPI_Barrier(PETSC_COMM_WORLD);
    MPI_Bcast(&globalresnorm,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Barrier(PETSC_COMM_WORLD);
    MPI_Bcast(&globalNonConverged,1,MPI_INT,0,PETSC_COMM_WORLD);
    MPI_Barrier(PETSC_COMM_WORLD);
    PetscPrintf(PETSC_COMM_WORLD, " ||Residual|| = %g; %i pts haven't converged\n", 
                globalresnorm,globalNonConverged
		);
    if (globalNonConverged == 0)
    {
      break;
    }

    /* Assemble the Jacobian in Struct of Arrays format where the physics
     * operations are all vectorized */
    for (int row=0; row < residual->numVars; row++)
    {
      /* Recommended value of Jacobian differencing parameter to achieve fp64
       * machine precision */
      double epsilon = params::JacobianAssembleEpsilon;

      array smallPrim = af::abs(primGuess.vars[row])<.5*epsilon;

      primGuessPlusEps->vars[row]  = 
          (1. + epsilon)*primGuess.vars[row]*(1.-smallPrim)
	      + smallPrim*epsilon; 

      computeResidual(*primGuessPlusEps, *residualPlusEps,
                      numReadsResidual, numWritesResidual
                     );

      for (int column=0; column < vars::numFluidVars; column++)
      {
        jacobianSoA(span, span, span, column + vars::numFluidVars*row)
          = (  residualPlusEps->vars[column] 
             - residual->vars[column]
            )
            /(primGuessPlusEps->vars[row]-primGuess.vars[row]);
      }
      /* reset */
      primGuessPlusEps->vars[row]  = primGuess.vars[row]; 
    }
    jacobianAssemblyTime += af::timer::stop(jacobianAssemblyTimer);
    /* Jacobian assembly complete */

    /* Solve the linear system Jacobian * deltaPrim = -residual for the
     * correction deltaPrim */

    array jacobianAoS = af::reorder(jacobianSoA, 3, 0, 1, 2);

    /* RHS of Ax = b in Array of Structs format */
    array bAoS = -af::reorder(residualSoA, 3, 0, 1, 2);


    /* Now solve Ax = b using direct inversion, where
     * A = Jacobian
     * x = deltaPrim
     * b = -residual  */
    batchLinearSolve(jacobianAoS, bAoS, deltaPrimAoS);

    /* Done with the solve. Now rearrange from AoS -> SoA */
    array deltaPrimSoA = af::reorder(deltaPrimAoS, 1, 2, 3, 0);

    /* Quadratic backtracking :
     We minimize f(u+stepLength*du) = 0.5*sqr(residual[u+stepLength*du]).
     We use
       f0 = f(u)
       fPrime0 = df/d(stepLength)(u) = -2*f0
       [because stepLength=1 is the solution of the linearized problem...]
       f1 = f(u+stepLength0*du)
     to reconstruct
       f = (f1-f0-fPrime0*stepLength0)*(stepLength/stepLength0)^2 + fPrime0*stepLength + f0
     which has a minimum at the new value of stepLength,
       stepLength = -fPrime0*stepLength0^2 / (f1-f0-fPrime0*stepLength0)/2
     */
    af::timer lineSearchTimer = af::timer::start();
    array f0      = 0.5 * l2Norm;
    array fPrime0 = -2.*f0;
    
    /* Start with a full step */
    stepLength = 1.;
    int lineSearchIter=0;
    for (;lineSearchIter < params::maxLineSearchIters; lineSearchIter++
        )
    {
      /* 1) First take current step stepLength */
      for (int var=0; var<vars::numFluidVars; var++)
      {
        primGuessLineSearchTrial->vars[var] =  
          primGuess.vars[var] + stepLength*deltaPrimSoA(span, span, span, var);
      } 
      /* ...and then compute the norm */
      computeResidual(*primGuessLineSearchTrial, *residual,
                      numReadsResidual, numWritesResidual
                     );
      for (int var=0; var<vars::numFluidVars; var++)
      {
        residualSoA(span, span, span, var) = residual->vars[var];
      }
      l2Norm = af::sum(af::pow(residualSoA(domainX1, domainX2, domainX3), 2.), 3);
      array f1 = 0.5 * l2Norm;

      /* We have 3 pieces of information:
       * a) f(0)
       * b) f'(0) 
       * c) f(stepLength) 
       */
    
      const double alpha    = 1e-4;
      const double EPS      = params::linesearchfloor;
      array stepLengthNoGhost = stepLength(domainX1, domainX2, domainX3);
      array condition = f1 > (f0*(1. - alpha*stepLengthNoGhost) +EPS);
      array denom     =   (f1-f0-fPrime0*stepLengthNoGhost) * condition 
                        + (1.-condition);
      array nextStepLengthNoGhost =
        -fPrime0*stepLengthNoGhost*stepLengthNoGhost/denom/2.;
      stepLength(domainX1, domainX2, domainX3)
        = stepLengthNoGhost*(1. - condition) + condition*nextStepLengthNoGhost;
      
      array conditionIndices = where(condition > 0);
      if (conditionIndices.elements() == 0)
      {
        break;
      }
    }

    /* stepLength has now been set */
    for (int var=0; var<vars::numFluidVars; var++)
    {
      primGuess.vars[var] = 
        primGuess.vars[var] + stepLength*deltaPrimSoA(span, span, span, var);
    }
    lineSearchTime += af::timer::stop(lineSearchTimer);
  }
}

void timeStepper::batchLinearSolve(const array &A, const array &b, array &x)
{
  af::timer linearSolverTimer = af::timer::start();

  int numVars = residual->numVars;
  int N1Total = residual->N1Total;
  int N2Total = residual->N2Total;
  int N3Total = residual->N3Total;

  if (params::linearSolver == linearSolvers::GPU_BATCH_SOLVER)
  {
    /* Resize A and b in order to pass into solve() */
    array AModDim = af::moddims(A, numVars, numVars,
                                N1Total * N2Total * N3Total
                               );
    array bModDim = af::moddims(b, numVars, 1,
                                N1Total * N2Total * N3Total
                               );

    array soln = af::solve(AModDim, bModDim);
    af::sync(); /* Need to sync() cause solve is non-blocking. 
                   Not doing so leads to erroneus performence metrics. */
  
    x = moddims(soln,
                numVars,
                prim->N1Total,
                prim->N2Total,
                prim->N3Total
               );
  }
  else if (params::linearSolver == linearSolvers::CPU_BATCH_SOLVER)
  {
    A.host(AHostPtr);
    b.host(bHostPtr);
  
    #pragma omp parallel for
    for (int k=0; k<N3Total; k++)
    {
      for (int j=0; j<N2Total; j++)
      {
        for (int i=0; i<N1Total; i++)
        {
          int pivot[numVars];
  
          const int spatialIndex = 
            i +  N1Total*(j + (N2Total*k) );
  
          LAPACKE_dgesv(LAPACK_COL_MAJOR, numVars, 1, 
                        &AHostPtr[numVars*numVars*spatialIndex], numVars, 
                        pivot, &bHostPtr[numVars*spatialIndex], numVars
                       );
  
        }
      }
    }
  
    /* Copy solution to x on device */
    x = array(numVars, N1Total, N2Total, N3Total, bHostPtr);
  }

  linearSolverTime += af::timer::stop(linearSolverTimer);
}

array pressureRho0W(const array rho0,
                    const array w
                   )
{
  array pressure = ((w - rho0)*(params::adiabaticIndex - 1.)/params::adiabaticIndex);
  //pressure.eval();

  return pressure;
}

array gammaFunc(const array &BSqr,
                const array &D,
                const array &QDotB,
                const array &QTildeSqr,
                const array &Wp
               )
{
  array QDotBWholeSqr = QDotB * QDotB;
  array W     = D + Wp;
  array WSqr  = W * W;
  array WB    = W + BSqr;
  array W2    = W*W;

  array uTildeSqr = - ( (W + WB)*QDotBWholeSqr + W2*QTildeSqr )
                    / (QDotBWholeSqr*(W + WB) + W2*(QTildeSqr - WB*WB)) ; 

  array gamma = af::sqrt(1 + af::abs(uTildeSqr));
  //gamma.eval();

  return gamma;
}

array errFunc(const array &BSqr,
              const array &D,
              const array &Ep,
              const array &QDotB,
              const array &QTildeSqr,
              const array &Wp
             )
{
  array W = Wp + D;
  array gamma = gammaFunc(BSqr, D, QDotB, QTildeSqr, Wp);
  array w = W/(gamma * gamma);
  array rho0 = D/gamma;
  array p = pressureRho0W(rho0, w);

  array err = - Ep + Wp - p + 0.5*BSqr 
              + 0.5*(BSqr*QTildeSqr - QDotB*QDotB)
              / ((BSqr + W)*(BSqr + W)) ;
  //err.eval();

  return err;
}

void timeStepper::idealSolver(grid &primGuess,
                            int &numReads,
                            int &numWrites
                           )
{
  elem->set(primGuess, *geomCenter,
            numReads, numWrites
           );
  
  array lapse = geomCenter->alpha;
  array g     = geomCenter->g;

  array D = cons->vars[vars::RHO] * lapse / g;

  array zero = 0. * elem->B1;

  array BCon[NDIM];
  BCon[0] = zero;
  BCon[1] = elem->B1 * lapse; //BCon[1].eval();
  BCon[2] = elem->B2 * lapse; //BCon[2].eval();
  BCon[3] = elem->B3 * lapse; //BCon[3].eval();

  array BCov[NDIM];
  for (int mu=0; mu<NDIM; mu++)
  {
    BCov[mu] =   geomCenter->gCov[mu][0] * BCon[0]
               + geomCenter->gCov[mu][1] * BCon[1]
               + geomCenter->gCov[mu][2] * BCon[2]
               + geomCenter->gCov[mu][3] * BCon[3];
    //BCov[mu].eval();
  }
  array BSqr =   BCov[0]*BCon[0] + BCov[1]*BCon[1]
               + BCov[2]*BCon[2] + BCov[3]*BCon[3];
  //BSqr.eval();

  array QCov[NDIM];
  QCov[0] = cons->vars[vars::U]  * lapse / g; //QCov[0].eval();
  QCov[1] = cons->vars[vars::U1] * lapse / g; //QCov[1].eval();
  QCov[2] = cons->vars[vars::U2] * lapse / g; //QCov[2].eval();
  QCov[3] = cons->vars[vars::U3] * lapse / g; //QCov[3].eval();

  array QCon[NDIM];
  for (int mu=0; mu<NDIM; mu++)
  {
    QCon[mu] =   geomCenter->gCon[mu][0] * QCov[0]
               + geomCenter->gCon[mu][1] * QCov[1]
               + geomCenter->gCon[mu][2] * QCov[2]
               + geomCenter->gCon[mu][3] * QCov[3];
    //QCon[mu].eval();
  }

  array nCov[NDIM];
  nCov[0] = -lapse;
  nCov[1] = zero;
  nCov[2] = zero;
  nCov[3] = zero;

  array nCon[NDIM];
  for (int mu=0; mu<NDIM; mu++)
  {
    nCon[mu] =   geomCenter->gCon[mu][0] * nCov[0]
               + geomCenter->gCon[mu][1] * nCov[1]
               + geomCenter->gCon[mu][2] * nCov[2]
               + geomCenter->gCon[mu][3] * nCov[3];
    //nCon[mu].eval();
  }

  array QDotn =   QCov[0]*nCon[0] + QCov[1]*nCon[1]
                + QCov[2]*nCon[2] + QCov[3]*nCon[3];
  //QDotn.eval();

  array QDotB =   QCov[0]*BCon[0] + QCov[1]*BCon[1]
                + QCov[2]*BCon[2] + QCov[3]*BCon[3];
  //QDotB.eval();
  
  array QSqr =   QCov[0]*QCon[0] + QCov[1]*QCon[1]
               + QCov[2]*QCon[2] + QCov[3]*QCon[3];
  //QSqr.eval();

  array QTildeCon[NDIM];
  for (int mu=0; mu < NDIM; mu++)
  {
    QTildeCon[mu] = QCon[mu] + nCon[mu]*QDotn;
  }
  array QTildeSqr = QSqr + QDotn*QDotn;

  array Ep = -QDotn - D;

  array uTildeCon[NDIM];

  uTildeCon[0] = zero;
  uTildeCon[1] = elem->u1;
  uTildeCon[2] = elem->u2;
  uTildeCon[3] = elem->u3;

  array uTildeCov[NDIM];
  for (int mu=0; mu<NDIM; mu++)
  {
    uTildeCov[mu] =   geomCenter->gCov[mu][0] * uTildeCon[0]
                    + geomCenter->gCov[mu][1] * uTildeCon[1]
                    + geomCenter->gCov[mu][2] * uTildeCon[2]
                    + geomCenter->gCov[mu][3] * uTildeCon[3];
  }

  array uTildeSqr =  uTildeCov[0] * uTildeCon[0] + uTildeCov[1] * uTildeCon[1]
                   + uTildeCov[2] * uTildeCon[2] + uTildeCov[3] * uTildeCon[3];

  array gammaTilde = af::sqrt(1. + af::abs(uTildeSqr));

  array Wp =   (elem->rho + elem->u + elem->pressure)*gammaTilde*gammaTilde
             - elem->rho*gammaTilde;

  double DEL = 1e-5;
  array WpMinus = (1. - DEL)*Wp;
  array h       = Wp - WpMinus;
  array WpPlus  = Wp + h;

  array errPlus  = errFunc(BSqr, Ep, D, QDotB, QTildeSqr, WpPlus );
  array err      = errFunc(BSqr, Ep, D, QDotB, QTildeSqr, Wp     );
  array errMinus = errFunc(BSqr, Ep, D, QDotB, QTildeSqr, WpMinus);

  array dErrdW   = (errPlus - errMinus)/(WpPlus - WpMinus);
  array dErr2dW2 = (errPlus - 2.*err + errMinus)/(h*h);

  array f = 0.5*err*dErr2dW2/(dErrdW * dErrdW);
  array dW = -err/dErrdW/(1. - af::min(af::max(-0.3, f), 0.3));

  array Wp1  = Wp;
  array err1 = err;

  Wp  = Wp + af::max( af::min(dW, 2.0*Wp), -0.5*Wp );
  err = errFunc(BSqr, D, Ep, QDotB, QTildeSqr, Wp);

  // ITERMAX is 8 in harm code. Need break?
  for (int iter=0; iter < 1; iter++)
  {
    dW  = (Wp1 - Wp)*err/(err - err1);

    Wp1  = Wp;
    err1 = err;

    Wp  = Wp + af::max( af::min(dW, 2.0*Wp), -0.5*Wp );
    err  = errFunc(BSqr, Ep, D, QDotB, QTildeSqr, Wp);
  }

  array gamma = gammaFunc(BSqr, D, QDotB, QTildeSqr, Wp);
  array rho0  = D/gamma;
  array W     = Wp + D;
  array w     = W/(gamma * gamma);
  array P     = pressureRho0W(rho0, w);
  
  primGuess.vars[vars::RHO] = rho0;
  primGuess.vars[vars::U]   = w - (rho0 + P);

  primGuess.vars[vars::U1] =   (gamma/(W + BSqr))
                             * (QTildeCon[1] + QDotB*BCon[1]/W) ;

  primGuess.vars[vars::U2] =   (gamma/(W + BSqr))
                             * (QTildeCon[2] + QDotB*BCon[2]/W) ;

  primGuess.vars[vars::U3] =   (gamma/(W + BSqr))
                             * (QTildeCon[3] + QDotB*BCon[3]/W) ;


  // Trivial fake version for testing 
  /*primGuess.vars[vars::RHO] = elem->rho;
  primGuess.vars[vars::U]   = elem->u;
  primGuess.vars[vars::U1]  = elem->u1;
  primGuess.vars[vars::U2]  = elem->u2;
  primGuess.vars[vars::U3]  = elem->u3;*/

  //for (int var=0; var <= vars::U3; var++)
  //{
  //  primGuess.vars[var].eval();
  //}

  std::vector<af::array *> arraysThatNeedEval{};
  for (int var=0; var <= vars::U3; var++)
  {
    arraysThatNeedEval.push_back(&primGuess.vars[var]);
  }                       
  af::eval(arraysThatNeedEval.size(), &arraysThatNeedEval[0]);
  
  return;
}

