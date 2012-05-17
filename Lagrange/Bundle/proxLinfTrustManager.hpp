#ifndef PROXLINFTRUSTMANGAGER_HPP
#define PROXLINFTRUSTMANGAGER_HPP

#include "bundleManager.hpp"

#include "cuttingPlaneBALP.hpp"
#include "lInfTrustBALP.hpp"
#include "lInfTrustBALP2.hpp"

// like levelManager, but only accept a step if it's in the right direction
template<typename BALPSolver, typename LagrangeSolver, typename RecourseSolver>
class proxLinfTrustManager : public bundleManager<BALPSolver,LagrangeSolver,RecourseSolver> {
public:
	proxLinfTrustManager(stochasticInput &input, BAContext & ctx) : 
		bundleManager<BALPSolver,LagrangeSolver,RecourseSolver>(input,ctx) {
		int nscen = input.nScenarios();
		trialSolution.resize(nscen,std::vector<double>(input.nFirstStageVars(),0.));
		rows2l.resize(nscen);
		cols2l.resize(nscen);
		maxRadius = 10.;
		curRadius = maxRadius/10.;
		counter = 0;
	}


protected: 
	virtual void doStep() {
		using namespace std;
		
		int nscen = this->input.nScenarios();
		int nvar1 = this->input.nFirstStageVars();
		
		printf("Iter %d Current Objective: %f\n",this->nIter-1,this->currentObj);
		if (this->terminated_) return;	
		

		{
			lInfTrustModel lm(nvar1,this->bundle,curRadius,this->currentSolution);
			BALPSolver solver(lm,this->ctx, (this->nIter > 1) ? BALPSolver::usePrimal : BALPSolver::useDual);
			
			if (this->nIter > 1) {
				for (int k = 0; k < lm.nFirstStageVars(); k++) {
					solver.setFirstStageColState(k,cols1l[k]);
				}
				for (int i = 0; i < nscen; i++) {
					for (int k = 0; k < lm.nSecondStageCons(i); k++) {
						solver.setSecondStageRowState(i,k,rows2l[i][k]);
					}
					cols2l[i].push_back(AtLower);
					for (int k = 0; k < lm.nSecondStageVars(i); k++) {
						solver.setSecondStageColState(i,k,cols2l[i][k]);
					}
				}
				solver.commitStates();
			}



			solver.go();
			lastModelObj = solver.getObjective();

			cols1l.resize(lm.nFirstStageVars());
			for (int k = 0; k < lm.nFirstStageVars(); k++) {
				cols1l[k] = solver.getFirstStageColState(k);
			}
			for (int i = 0; i < nscen; i++) {
				rows2l[i].resize(lm.nSecondStageCons(i));
				for (int k = 0; k < lm.nSecondStageCons(i); k++) {
					rows2l[i][k] = solver.getSecondStageRowState(i,k);
				}
				cols2l[i].resize(lm.nSecondStageVars(i));
				for (int k = 0; k < lm.nSecondStageVars(i); k++) {
					cols2l[i][k] = solver.getSecondStageColState(i,k);
				}
			}

			double maxdiff = 0.;
			for (int i = 0; i < nscen; i++) {
				std::vector<double> const& iterate = solver.getSecondStageDualRowSolution(i);
				for (int k = 0; k < nvar1; k++) {
					trialSolution[i][k] = -iterate[k+1];
					maxdiff = max(fabs(trialSolution[i][k]-this->currentSolution[i][k]),maxdiff);
				}
			}

			double newObj = this->evaluateSolution(trialSolution);
			cout << "Trial solution has obj = " << newObj;
			if (newObj > this->currentObj) {
				cout << ", accepting\n";
				if (maxdiff > curRadius - 1e-5 && newObj > this->currentObj + 0.5*(lastModelObj-this->currentObj)) {
					curRadius = min(2.*curRadius,maxRadius);
					cout << "Increased trust region radius to " << curRadius << endl;
				}
				swap(this->currentSolution,trialSolution);
				this->currentObj = newObj;
				counter = 0;
				if (fabs(lastModelObj-this->currentObj)/fabs(this->currentObj) < this->relativeConvergenceTol) {
					this->terminated_ = true;
					//checkLastPrimals();
				}

			} else {
				cout << ", null step\n";
				double rho = min(1.,curRadius)*(newObj-this->currentObj)/(this->currentObj-lastModelObj);
				cout << "Rho: " << rho << " counter: " << counter << endl;
				if (rho > 0) counter++;
				if (rho > 3 || (counter >= 3 && 1. < rho && rho <= 3.)) {
					curRadius *= 1./min(rho,4.);
					cout << "Decreased trust region radius to " << curRadius << endl;
					counter = 0;
				}
			}
		}


	}

private:
	double lastModelObj;
	double maxRadius, curRadius;
	int counter;
	// saved states for regularized cutting plane lp
	std::vector<std::vector<variableState> > rows2l, cols2l;
	std::vector<variableState> cols1l;

	std::vector<std::vector<double> > trialSolution; 


};


#endif