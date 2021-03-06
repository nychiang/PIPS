#ifndef BUNDLEMANAGER_HPP
#define BUNDLEMANAGER_HPP

#include "BA.hpp"
#include "BALPSolverInterface.hpp"
#include "LagrangeSubproblemInterface.hpp"
#include "CoinFinite.hpp"
#include <boost/shared_ptr.hpp>
#include <sstream>
#include <fstream>

using boost::shared_ptr;

struct cutInfo {
	std::vector<double> evaluatedAt, subgradient, primalSol;
	double objval; // underestimate of convex objective
	double objmax; // overestimate of convex objective
	double computeC() const; // compute component of c as defined in cuttingPlaneBALP.hpp
	double computeC(std::vector<double> const& proxCenter) const; // alternate definition, see proximalBAQP.hpp
	double baseobj; // "c^Tx" part of the objective, without multiplier contribution (\lambda^Tx)
	double z; // for temporarily storing multiplier on this cut
	// could include error estimates here later
};


// outer dimension is scenario, inner is list of cuts.
// this allows bundle to be distributed per scenario, i.e,
// if a scenario s isn't assigned, bundle[s] will be empty. 
typedef std::vector<std::vector<cutInfo> > bundle_t; 


// this is specialized particularly for lagrangian relaxation of nonanticipativity constraints, not general bundle solver
template<typename BALPSolver, typename LagrangeSolver, typename RecourseSolver> class bundleManager {
public:
	bundleManager(stochasticInput &input, BAContext & ctx) : ctx(ctx), input(input) {
		int nscen = input.nScenarios();
		// use zero as initial iterate
		if (!BALPSolver::isDistributed()) {
			currentSolution.resize(nscen,std::vector<double>(input.nFirstStageVars(),0.));
		} else {
			currentSolution.resize(nscen);
			std::vector<int> const& localScen = ctx.localScenarios();
			for (unsigned i = 1; i < localScen.size(); i++) {
				int scen = localScen[i];
				currentSolution[scen].resize(input.nFirstStageVars());
			}
		}
		nIter = -1;
		bundle.resize(nscen);
		hotstarts.resize(nscen);
		recourseRowStates.resize(nscen);
		recourseColStates.resize(nscen);
		bestPrimalObj = COIN_DBL_MAX;
		relativeConvergenceTol = 1e-7;
		terminated_ = false;
	}


	void setRelConvergenceTol(double t) { relativeConvergenceTol = t; }
	bool terminated() { return terminated_; }
	void iterate();

protected:

	virtual void doStep() = 0;
	int solveSubproblem(std::vector<double> const& at, int scen, double &obj, double eps_sol = 0.); // returns number of cuts added
	double testPrimal(std::vector<double> const& primal); // test a primal solution and return objective (COIN_DBL_MAX) if infeasible
	// evaluates trial solution and updates the bundle
	double evaluateSolution(std::vector<std::vector<double> > const& sol, double eps_sol = 0.);

	void evaluateAndUpdate();
	void checkLastPrimals();
	
	bundle_t bundle;
	std::vector<std::vector<double> > currentSolution; // dual solution
	double currentObj;
	std::vector<double> bestPrimal;
	double bestPrimalObj;
	BAContext &ctx;
	stochasticInput &input;
	int nIter;
	double relativeConvergenceTol;
	bool terminated_;
	std::vector<shared_ptr<typename LagrangeSolver::WarmStart> > hotstarts;
	std::vector<std::vector<variableState> > recourseRowStates, recourseColStates;
	


};


template<typename B,typename L, typename R> int bundleManager<B,L,R>::solveSubproblem(std::vector<double> const& at, int scen, double &obj, double eps_sol) {
	using namespace std;	
	int nvar1 = input.nFirstStageVars();
	//double t = MPI_Wtime();
	

	L lsol(input,scen,at);
	//lsol.setAbsoluteGap(eps_sol);
	// hot start for root node LP
	/*if (hotstarts[scen]) {
		lsol.setWarmStart(*hotstarts[scen].get());
	} else if (hotstarts[ctx.localScenarios().at(1)]) {
		lsol.setWarmStart(*hotstarts[ctx.localScenarios()[1]].get());
	}*/
	//lsol.setRatio(100*fabs(relprec));
	lsol.go();
	//hotstarts[scen].reset(lsol.getWarmStart());
	//cout << "SCEN " << scen << " DONE, " << MPI_Wtime() - t << " SEC" << endl;

	//assert(lsol.getStatus() == Optimal);
	assert(lsol.getStatus() != ProvenInfeasible);
	obj += lsol.getBestPossibleObjective();
	int nadded = 0;
	// Uncomment this block to return suboptimal solutions
	/*
	std::vector<PrimalSolution> sols = lsol.getBestFirstStageSolutions(0.5); // 0.5% cutoff
	for (unsigned k = 0; k < sols.size(); k++) {
		cutInfo cut;
		cut.objmax = -lsol.getBestPossibleObjective();
		cut.objval = -sols[k].objval;
		cut.primalSol = sols[k].sol;
		cut.evaluatedAt = at;
		cut.subgradient.resize(nvar1);
		cut.baseobj = cut.objval;
		for (int i = 0; i < nvar1; i++) {
			cut.subgradient[i] = -cut.primalSol[i];
			cut.baseobj -= cut.subgradient[i]*cut.evaluatedAt[i];
		}
		bool foundDuplicate = false;
		for (unsigned r = 0; r < bundle[scen].size(); r++) {
			bool alleq = true;
			for (int i = 0; i < nvar1; i++) {
				if (fabs(cut.subgradient[i]-bundle[scen][r].subgradient[i]) > 1e-10) {
					alleq = false;
					break;
				}
			}
			if (alleq) {
				foundDuplicate = true;
				// keep the tighter cut
				if (bundle[scen][r].baseobj > cut.baseobj) {
					cut = bundle[scen][r];
				}
				bundle[scen].erase(bundle[scen].begin()+r);
				nadded--;
				//printf("got duplicate cut\n");
				break;
			}
		}
		bundle[scen].push_back(cut);
		nadded++;
	}
	*/
	// Uncomment this block to return only the best solution
	
	cutInfo cut;
	nadded = 1;
	cut.objval = -lsol.getBestFeasibleObjective();
	cut.objmax = -lsol.getBestPossibleObjective();
	cut.primalSol = lsol.getBestFirstStageSolution(); 
	cut.evaluatedAt = at;
	cut.subgradient.resize(nvar1);
	for (int i = 0; i < nvar1; i++) {
		cut.subgradient[i] = -cut.primalSol[i];
	}
	bundle[scen].push_back(cut);
	
	
	return nadded;

}


template<typename B, typename L, typename R> double bundleManager<B,L,R>::testPrimal(std::vector<double> const& primal) {

	const std::vector<double> &obj1 = input.getFirstStageObj();
	const std::vector<int> &localScen = ctx.localScenarios();
	int nvar1 = input.nFirstStageVars();
	double obj = 0.;
	for (unsigned i = 1; i < localScen.size(); i++) {
		int scen = localScen[i];
		int nvar2 = input.nSecondStageVars(scen);
		int ncons2 = input.nSecondStageCons(scen);
		R rsol(input,scen,primal);
		rsol.setDualObjectiveLimit(1e10);
		int statesFromScen = -1;
		if (input.continuousRecourse()) {
			if (recourseRowStates[scen].size()) statesFromScen = scen;
			else if (recourseRowStates[localScen.at(1)].size()) statesFromScen = localScen[1];
		}
		if (statesFromScen != -1) {
			for (int k = 0; k < nvar2; k++) {
				rsol.setSecondStageColState(k,recourseColStates[statesFromScen][k]);
			}
			for (int k = 0; k < ncons2; k++) {
				rsol.setSecondStageRowState(k,recourseRowStates[statesFromScen][k]);
			}
		}
		
		rsol.go();
		if (input.continuousRecourse()) {
			recourseColStates[scen].resize(nvar2);
			for (int k = 0; k < nvar2; k++) {
				recourseColStates[scen][k] = rsol.getSecondStageColState(k);
			}
			recourseRowStates[scen].resize(ncons2);
			for (int k = 0; k < ncons2; k++) {
				recourseRowStates[scen][k] = rsol.getSecondStageRowState(k);
			}
		}

		obj += rsol.getObjective();
		
		if (rsol.getStatus() == ProvenInfeasible) {
			printf("got infeasible 1st stage\n");
			obj = COIN_DBL_MAX;
			break;
		}
	}
	double allobj;
	MPI_Allreduce(&obj,&allobj,1,MPI_DOUBLE,MPI_SUM,ctx.comm());
	for (int k = 0; k < nvar1; k++) {
		allobj += primal[k]*obj1[k];
	}
	
	return allobj;

}

template<typename B, typename L, typename R> double bundleManager<B,L,R>::evaluateSolution(std::vector<std::vector<double> > const& sol, double eps_sol) {

	std::vector<int> const& localScen = ctx.localScenarios();
	int nscen = input.nScenarios();
	double obj = 0.;
	std::vector<int> cutsAdded(nscen,0);
	int totalExtraCuts = 0;
	for (unsigned i = 1; i < localScen.size(); i++) {
		int scen = localScen[i];
		assert(sol[scen].size());
		int ncuts = solveSubproblem(sol[scen],scen, obj, eps_sol);
		cutsAdded[scen] = ncuts;
		totalExtraCuts += ncuts-1;
	}
	double locobj = obj;
	MPI_Allreduce(&locobj,&obj,1,MPI_DOUBLE,MPI_SUM,ctx.comm());
	
	if (!B::isDistributed()) {
		// only need to do this if we have a serial solver
		int nvar1 = input.nFirstStageVars();
		for (int scen = 0; scen < nscen; scen++) {
			int proc = ctx.owner(scen);
			int cutsToAdd = cutsAdded[scen];
			MPI_Bcast(&cutsToAdd,1,MPI_INT,proc,ctx.comm());
			for (int k = 0; k < cutsToAdd; k++) {
				int i;
				if (ctx.mype() != proc) {
					i = bundle[scen].size();
					bundle[scen].push_back(cutInfo());
					bundle[scen][i].primalSol.resize(nvar1);
					bundle[scen][i].evaluatedAt.resize(nvar1);
					bundle[scen][i].subgradient.resize(nvar1);
				} else {
					i = bundle[scen].size()-cutsToAdd+k;
				}
				MPI_Bcast(&bundle[scen][i].primalSol[0],nvar1,MPI_DOUBLE,proc,ctx.comm());
				MPI_Bcast(&bundle[scen][i].evaluatedAt[0],nvar1,MPI_DOUBLE,proc,ctx.comm());
				MPI_Bcast(&bundle[scen][i].subgradient[0],nvar1,MPI_DOUBLE,proc,ctx.comm());
				MPI_Bcast(&bundle[scen][i].objval,1,MPI_DOUBLE,proc,ctx.comm());
				MPI_Bcast(&bundle[scen][i].objmax,1,MPI_DOUBLE,proc,ctx.comm());
			}	
			
		}
		
	} 
	int tot = totalExtraCuts;
	MPI_Allreduce(&tot,&totalExtraCuts,1,MPI_INT,MPI_SUM,ctx.comm());
	if (ctx.mype() == 0) printf("Added %f extra cuts per scenario\n",totalExtraCuts/(double)nscen);
	return obj;
}

template<typename B, typename L, typename R> void bundleManager<B,L,R>::evaluateAndUpdate() {

	currentObj = evaluateSolution(currentSolution);
}

template<typename B, typename L, typename R> void bundleManager<B,L,R>::checkLastPrimals() {
	using namespace std;

	int nscen = input.nScenarios();
	int nvar1 = input.nFirstStageVars();
	/*ofstream f;
	stringstream ss;
	ss << "sols" << nIter;
	if (ctx.mype() == 0) f.open(ss.str().c_str());*/
	for (int i = 0; i < nscen; i++) {
		int proc = ctx.owner(i);
		std::vector<double> at(nvar1), p(nvar1);
		if (proc == ctx.mype()) {
			assert(bundle[i].size());
			at = bundle[i][bundle[i].size()-1].evaluatedAt;
			p = bundle[i][bundle[i].size()-1].primalSol;
		}
		MPI_Bcast(&at[0],nvar1,MPI_DOUBLE,proc,ctx.comm());
		MPI_Bcast(&p[0],nvar1,MPI_DOUBLE,proc,ctx.comm());
		/*if (ctx.mype() == 0) {
			for (int k = 0; k < nvar1; k++) {
				f << p[k] << " " << at[k] << " ";
			}
			f << endl;
		}*/
		double o = 5000.;// = testPrimal(p);
		if (o < bestPrimalObj) {
			bestPrimalObj = o;
			bestPrimal = p;
		}
	}
}


template<typename B, typename L, typename R> void bundleManager<B,L,R>::iterate() {

	assert(!terminated_);
	

	if (nIter++ == -1) { // just evaluate and generate subgradients
		evaluateAndUpdate();
		return;
	}

	checkLastPrimals();
	doStep();
	
}

#endif
