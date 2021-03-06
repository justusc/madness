/*
 * TDHF.cc
 *
 *  Created on: Aug 11, 2016
 *      Author: kottmanj
 */

#include "TDHF.h"

namespace madness {

  TDHF::TDHF(World &world, const CC_Parameters & param, const Nemo & nemo_):
						      world(world),
						      parameters(param),
						      nemo(nemo_),
						      g12(world,g12_,param),
						      mo_ket_(make_mo_ket(nemo_)),
						      mo_bra_(make_mo_bra(nemo_)),
						      Q(world,mo_bra_.get_vecfunction(),mo_ket_.get_vecfunction()),
						      msg(world) {
    msg.output_subsection("Computing Exchange Intermediate");
    CC_Timer time(world,"Computing ExIm");
    g12.update_elements(mo_bra_,mo_ket_);
    time.info();
    // TODO Auto-generated constructor stub

  }

  void TDHF::initialize(std::vector<CC_vecfunction> &start)const{
    msg.output_subsection("Calculate Guess");
    std::vector<CC_vecfunction> guess;
    if(parameters.tda_homo_guess) guess= make_homo_guess();
    else guess = make_guess();

    // combine guess and start vectors
    for(const auto& tmp:start) guess.push_back(tmp);

    if(parameters.tda_homo_guess){
      // ortho only needed if there where start vectors
      if(not start.empty()){
	std::vector<vecfuncT> empty;
	orthonormalize(guess,empty);
      }
    }else{
      std::vector<vecfuncT> empty;
      orthonormalize(guess,empty);
    }

    // failsafe
    if(guess.size()<parameters.tda_guess_excitations){
      std::string msg=("WARNING: You demanded: " + std::to_string(parameters.tda_guess_excitations)
      + " Guess vectors, but your demanded guess has only "
      + std::to_string(guess.size()) + "vectors. So we will not iterate the first vectors and then do the same guess again ... this might be unstable").c_str();
      iterate_cis_guess_vectors(guess);
      initialize(guess);
    }

    //sort guess (according to excitation energies)
    std::sort(guess.begin(),guess.end());
    //truncate the guess
    std::vector<CC_vecfunction> guess_vectors;
    for(size_t i=0;i<parameters.tda_guess_excitations;i++) guess_vectors.push_back(guess[i]);
    // this is the return value
    start = guess_vectors;
  }


  void TDHF::solve_cis(std::vector<CC_vecfunction> &start)const{
    msg.output_section("SOLVING CIS EQUATIONS");
    parameters.print_tda_parameters(world);

    mo_ket_.plot("MOS_");

    CC_Timer time(world,"TDHF/CIS");
    // decide if a guess calculation is needed
    bool need_guess =false;
    if(start.size()<parameters.tda_guess_excitations) need_guess=true;
    std::vector<CC_vecfunction> guess_vectors;
    if(need_guess){
      initialize(start);
      guess_vectors = start;
    }else guess_vectors=start;

    std::vector<CC_vecfunction> final_vectors;
    msg.output_subsection("Iterate Guess Vectors");
    {
      // do guess iterations
      iterate_cis_guess_vectors(guess_vectors);
      // sort according to excitation energies
      std::sort(guess_vectors.begin(),guess_vectors.end());
      // save
      for(size_t i=0;i<guess_vectors.size();i++) guess_vectors[i].save(std::to_string(i));
      // truncate again
      for(size_t i=0;i<parameters.tda_excitations;i++) final_vectors.push_back(guess_vectors[i]);
    }

    msg.output_subsection("Iterate Final Vectors");
    iterate_cis_final_vectors(final_vectors);
    msg.output_section("CIS CALCULATIONS ENDED");
    std::sort(final_vectors.begin(),final_vectors.end());
    // information
    if(world.rank()==0) std::cout << std::setfill('-') << std::setw(25) << "\n" << std::setfill(' ');
    if(world.rank()==0) std::cout << "Results of CIS Calculation: Excitation, Excitation Energy, WF-Error \n";
    for(size_t i=0;i<final_vectors.size();i++){
      if(world.rank()==0) std::cout << "Excitation " << std::fixed << std::setprecision(1) << i <<": "
	  << std::fixed << std::setprecision(10) << final_vectors[i].omega
	  << std::scientific << std::setprecision(2)  << ", " << final_vectors[i].current_error << "\n";
    }
    if(world.rank()==0) std::cout << std::setfill('-') << std::setw(25) << "\n" << std::setfill(' ');
    time.info();
    // plot final vectors
    for(size_t i=0;i<final_vectors.size();i++) final_vectors[i].plot(std::to_string(i)+"_converged_cis");
    // save final vectors
    for(size_t i=0;i<final_vectors.size();i++) final_vectors[i].save(std::to_string(i));
    // assign solution vectors
    start = final_vectors;
  }

  void TDHF::solve_tdhf(std::vector<CC_vecfunction> &x)const{
    msg.output_section("SOLVING TDHF EQUATIONS");

    // first solve CIS as first step
    solve_cis(x);

    // initialize the y states as zero functions
    vecfuncT zeros = zero_functions<double,3>(world,mo_ket_.size()-parameters.freeze);
    std::vector<CC_vecfunction> y;
    for(size_t i=0;i<x.size();i++){
      CC_vecfunction tmp(madness::copy(world,zeros),UNDEFINED,parameters.freeze);
      tmp.omega = x[i].omega*(-1.0);
      y.push_back(tmp);
    }
    y = make_y_guess(x,y);

    for(size_t macro=0;macro<10;macro++){
      msg.output_section("TDHF MACROITERATION " + std::to_string(macro));
      // iterate y vectors
      msg.output_subsection("Iterate y-vectors");
      bool yconv = iterate_vectors(y,x,true,parameters.tda_dconv,parameters.tda_econv,parameters.tda_iter_max,true);
      // iterate x vectors
      msg.output_subsection("Iterate x-vectors");
      bool xconv = iterate_vectors(x,y,false,parameters.tda_dconv,parameters.tda_econv,parameters.tda_iter_max,true);
      // check convergence
      if(yconv and xconv) break;
    }
    for(size_t i=0;i<x.size();i++) x[i].plot(std::to_string(i)+"_converged_tdhf_x");
    for(size_t i=0;i<y.size();i++) y[i].plot(std::to_string(i)+"_converged_tdhf_y");
  }

  bool TDHF::iterate_cis_guess_vectors(std::vector<CC_vecfunction> &x)const{
    std::vector<CC_vecfunction> dummy;
    return iterate_vectors(x,dummy,false,parameters.tda_dconv_guess,parameters.tda_econv_guess,parameters.tda_iter_guess, false);
  }
  bool TDHF::iterate_cis_final_vectors(std::vector<CC_vecfunction> &x)const{
    std::vector<CC_vecfunction> dummy;
    return iterate_vectors(x,dummy,false,parameters.tda_dconv,parameters.tda_econv,parameters.tda_iter_max, parameters.kain);
  }
  bool TDHF::iterate_vectors(std::vector<CC_vecfunction> &x,const std::vector<CC_vecfunction> &y,const bool iterate_y,const double dconv, const double econv, const double iter_max, const bool kain)const{
    if(iterate_y) MADNESS_ASSERT(x.size()==y.size());

    // set up the kain solvers ... if needed or not
    std::vector<XNonlinearSolver<vecfuncT,double,TDHF_allocator> > solvers;
    // initialize solvers
    if(kain){
      for(size_t i=0;i<x.size();i++){
	XNonlinearSolver<vecfuncT,double,TDHF_allocator>  solver(TDHF_allocator(world,x[i].size()),true);
	solver.set_maxsub(parameters.kain_subspace);
	solvers.push_back(solver);
      }
    }

    bool converged = true;

    // get potentials (if demanded)
    std::vector<vecfuncT> V;

    // for TDHF the potential is always stored
    if(parameters.tda_store_potential and y.empty()) V = make_potentials(x);
    else if(not y.empty()) V = make_tdhf_potentials(x,y);
    // orthonormalize
    orthonormalize(x,V);

    for(size_t iter=0;iter<iter_max;iter++){
      // if we solve for the y functions we need to switch the sign of omega
      if(iterate_y){
	for(auto& tmp:x) tmp.omega *=-1.0;
      }
      // apply Greens Functions
      for(size_t i=0;i<x.size();i++) if(iterate_y and x[i].omega>=0.0) x[i].omega = -1.0*y[i].omega;
      std::vector<vecfuncT> residuals=apply_G(x,V);
      // check convergence

      converged = true;
      std::vector<double> errors;
      for(size_t i=0;i<x.size();i++){
	double error = norm2(world,residuals[i]);
	if(error>dconv) converged=false;
	errors.push_back(error);
      }
      // update, store old omegas in the deltas vector
      std::vector<double> deltas;
      for(size_t i=0;i<x.size();i++){
	vecfuncT new_x0;
	if(kain){
	  new_x0 = solvers[i].update(x[i].get_vecfunction(),residuals[i],10.0*parameters.thresh_3D,5.0);
	}else{
	  new_x0 = sub(world,x[i].get_vecfunction(),residuals[i]);
	}
	vecfuncT Q_new_x0 = Q(new_x0);
	truncate(world,Q_new_x0);
	CC_vecfunction new_x1(Q_new_x0,x[i].type,parameters.freeze);

	new_x1.omega = x[i].omega;
	deltas.push_back(x[i].omega);
	x[i]=new_x1;
      }
      // for TDHF the potential is always stored
      if(parameters.tda_store_potential and y.empty()) V = make_potentials(x);
      else if(not y.empty()) V = make_tdhf_potentials(x,y);
      // orthonormalize
      orthonormalize(x,V);
      // save functions (every 5 iterations)
      if(iter%5==0){
	if(not iterate_y)for(size_t i=0;i<x.size();i++) x[i].save(std::to_string(i));
	else for(size_t i=0;i<y.size();i++) y[i].save(std::to_string(i));
      }
      // make plots if demanded
      if(parameters.plot){
	if(iterate_y) for(size_t i=0;i<y.size();i++) y[i].plot(std::to_string(i)+"_y_iter_"+std::to_string(iter)+"_");
	else for(size_t i=0;i<x.size();i++) x[i].plot(std::to_string(i)+"_iter_"+std::to_string(iter)+"_");
      }
      // information
      if(world.rank()==0) std::cout << std::setfill('-') << std::setw(60) << "\n" << std::setfill(' ');
      if(world.rank()==0) std::cout << "Iteration " << iter <<": omega, error, delta "<< "\n";
      for(size_t i=0;i<errors.size();i++){
	if(world.rank()==0) std::cout << "Excitation " << std::fixed << std::setprecision(1) << i <<": "
	    << std::fixed << std::setprecision(10) << x[i].omega
	    << std::scientific << std::setprecision(2)  << ", " << errors[i] << ", " << x[i].omega-deltas[i] << "\n";
	x[i].current_error=errors[i];
      }
      if(world.rank()==0) std::cout << std::setfill('-') << std::setw(60) << "\n" << std::setfill(' ');

      if(converged) break;
    }
    return converged;
  }



  std::vector<vecfuncT>  TDHF::apply_G(std::vector<CC_vecfunction> &x, const std::vector<vecfuncT> &V)const{

    std::string msg1 = "Applying Greens Function to vectors";
    if(V.empty()) msg1+=", with recalculated Potentials";
    CC_Timer time(world,msg1);
    std::vector<vecfuncT> result;
    for(size_t i=0;i<x.size();i++){

      vecfuncT Vi;
      if(V.empty()) Vi=get_tda_potential(x[i]);
      else Vi=V[i];
      double omega = x[i].omega;
      if(x[i].type==RESPONSE) MADNESS_ASSERT(omega>0.0);
      else MADNESS_ASSERT(omega<0.0);
      if(x[i].type==UNDEFINED and V.empty()) msg.warning("Empty V but x is y state from TDHF");

      // scale potential
      scale(world,Vi,-2.0);

      // make bsh operators as pointers in order to apply them in parallel
      std::vector<std::shared_ptr<SeparatedConvolution<double,3> > > bsh(x[i].size());
      for (size_t p = 0; p < bsh.size(); p++) {
	double eps = get_orbital_energy(p+parameters.freeze) + omega;
	// if eps is above zero we have an unbound state (or are early in the iteration) however this needs a shift of the potential
	// we shift to -0.05 (same as moldft does, no particular reason)
	if(eps>0.0){
	  msg.output("potential shift needed for V" + std::to_string(p+parameters.freeze));
	  double shift = eps+0.05;
	  eps = eps - shift;
	  Vi[p] -= (-2.0*shift*x[i].get_vecfunction()[p]);
	}
	if(eps>0.0){
	  msg.warning("eps is " + std::to_string(eps) + "... should not happen ... setting to zero");
	  eps = -1.0*parameters.thresh_3D;
	}
	MADNESS_ASSERT(not(eps>0.0));
	bsh[p] = poperatorT(BSHOperatorPtr3D(world, sqrt(-2.0 * eps), parameters.lo, parameters.thresh_bsh_3D));
      }
      world.gop.fence();

      vecfuncT GV = apply(world, bsh, Vi);

      vecfuncT residual = sub(world,x[i].get_vecfunction(),GV);
      result.push_back(residual);

      // Calculate Second Order Energy Update
      {
	// Inner product of Vpsi and the residual (Vi is scaled to -2.0 --> multiply later with 0.5)
	double tmp = inner(world,make_bra(residual),Vi).sum();
	// squared norm of GVpsi (Psi_tilde)
	double tmp2 = inner(world,make_bra(GV),GV).sum();

	// Factor 0.5 removes the factor 2 from the scaling before
	x[i].delta = (0.5 * tmp / tmp2);
      }
    }
    time.info();
    return result;
  }

  std::vector<vecfuncT> TDHF::make_potentials(const std::vector<CC_vecfunction> &x)const{
    CC_Timer time(world,"Make Potentials");
    std::vector<vecfuncT> V;
    for(auto& xi:x){
      if(world.rank()==0 and parameters.debug) std::cout << std::setfill('-') << std::setw(60) << "\n" << std::setfill(' ');
      const vecfuncT pot = get_tda_potential(xi);
      V.push_back(pot);
      if(world.rank()==0 and parameters.debug) std::cout << std::setfill('-') << std::setw(60) << "\n" << std::setfill(' ');
    }
    time.info();
    MADNESS_ASSERT(V.size()==x.size());
    return V;
  }

  vecfuncT TDHF::get_tda_potential(const CC_vecfunction &x)const{
    // XC information
    const std::string xc_data = nemo.get_calc()->param.xc_data;
    // HF exchange Coefficient
    double hf_coeff = nemo.get_calc()->xc.hf_exchange_coefficient();
    // Use the PCMSolver
    bool pcm=nemo.do_pcm();
    if(parameters.debug){
      if(world.rank()==0) std::cout << "TDA Potential is " << xc_data << ", hf_coeff=" << hf_coeff << ", pcm is=" << pcm <<  "\n";
    }
    if(hf_coeff<0.0) msg.warning("hf_exchange_coefficient is negative");

    // Occupation numbers
    const Tensor<double> occ=nemo.get_calc()->get_aocc();
    // Closed shell full density of the nemo orbitals (without any nuclear cusps)
    const real_function_3d nemo_density=2.0*nemo.make_density(occ,mo_ket_.get_vecfunction());
    // Real Alpha density (with nuclear cusps)
    const real_function_3d alpha_density=0.5*nemo.R_square*nemo_density;

    // XC Potential
    const XCOperator xc(world,xc_data, not nemo.get_calc()->param.spin_restricted,alpha_density,alpha_density);

    // Apply Ground State Potential to x-states
    vecfuncT Vpsi1;
    {
      // construct unperturbed operators
      const Coulomb J(world,&nemo);
      const Nuclear V(world,&nemo);


      std::string xc_data=nemo.get_calc()->param.xc_data;
      xc_data = xc_data.erase(0,xc_data.find_first_not_of(" "));
      xc_data = xc_data.erase(xc_data.find_last_not_of(" ")+1);

      // Nuclear Potential applied to x
      CC_Timer timeN(world,"Nx");
      const vecfuncT Nx=V(x.get_vecfunction());
      timeN.info(parameters.debug);
      // Applied Hartree Potential (J|x>) -> factor two is absorbed into the density for the J Operator
      CC_Timer timeJ(world,"Jx");
      const vecfuncT Jx=J(x.get_vecfunction());
      timeJ.info(parameters.debug);
      // Applied XC Potential
      CC_Timer timeXCx(world,"XCx");
      const vecfuncT XCx=xc(x.get_vecfunction());
      timeXCx.info(parameters.debug);
      // Ground State Potential applied to x, without exchange
      Vpsi1 = Nx+Jx+XCx;
      // add exchange if demanded
      if(hf_coeff!=0.0){
	CC_Timer timeKx(world,"Kx");
	Exchange K=Exchange(world,&nemo,0).small_memory(false);
	K.set_parameters(mo_bra_.get_vecfunction(),mo_ket_.get_vecfunction(),occ,parameters.lo,parameters.thresh_poisson);
	vecfuncT Kx =K(x.get_vecfunction());
	scale(world,Kx,hf_coeff);
	Vpsi1 = sub(world, Vpsi1, Kx);
	timeKx.info(parameters.debug);
      }
      // compute the solvent (PCM) contribution to the potential
      if (pcm) {
	CC_Timer timepcm(world,"pcm:gs");
        const real_function_3d vpcm = nemo.get_pcm().compute_pcm_potential(J.potential(),false);
        if(parameters.plot or parameters.debug) plot_plane(world,vpcm,"vpcm_gs");
        const vecfuncT pcm_x=vpcm*x.get_vecfunction();
        timepcm.info(parameters.debug);
        Vpsi1 = add(world,Vpsi1,pcm_x);
      }


      truncate(world,Vpsi1);
    }

    // Apply the Perturbed Potential to the Active Ground State Orbitals
    vecfuncT Vpsi2;
    {
      // active mo
      const vecfuncT active_mo = get_active_mo_ket();
      const vecfuncT active_bra = get_active_mo_bra();
      // construct perturbed operators
      CC_Timer timeJ(world,"pXC");
      Coulomb Jp(world);
      real_function_3d density_pert=2.0*nemo.make_density(occ,active_bra,x.get_vecfunction());
      Jp.potential()=Jp.compute_potential(density_pert);
      // reconstruct the full perturbed density: do not truncate!
      real_function_3d gamma=xc.apply_xc_kernel(density_pert);
      vecfuncT XCp=mul(world,gamma,active_mo);
      truncate(world,XCp);

      Vpsi2 = Jp(active_mo)+XCp;
      timeJ.info(parameters.debug);
      // Exchange Part
      if(hf_coeff>0.0){
	CC_Timer timeK(world,"pK");
	vecfuncT Kp;
	// summation over all active indices
	for(const auto itmp:x.functions){
	  const size_t i=itmp.first;
	  real_function_3d Ki=real_factory_3d(world);
	  for(const auto ktmp:x.functions){
	    const size_t k=ktmp.first;
	    Ki+=(g12(mo_bra_(k),mo_ket_(i))*x(k).function).truncate();
	  }
	  Kp.push_back(Ki);
	}
	scale(world,Kp,hf_coeff);
	Vpsi2 = sub(world,Vpsi2,Kp);
	timeK.info(parameters.debug);
      }

      // compute the solvent (PCM) contribution to the kernel
      if (pcm) {
	CC_Timer timepcm(world,"pcm:ex");
        const real_function_3d vpcm = nemo.get_pcm().compute_pcm_potential(Jp.potential(),true);
        if(parameters.plot or parameters.debug) plot_plane(world,vpcm,"vpcm_ex");
        const vecfuncT pcm_orbitals=vpcm*active_mo;
        timepcm.info(parameters.debug);
        Vpsi2 = add(world,Vpsi2,pcm_orbitals);
      }

      truncate(Vpsi2);
    }
    // whole tda potential
    vecfuncT Vpsi = Vpsi1 + Q(Vpsi2);
    truncate(world,Vpsi);

    // debug output
    if(parameters.debug or parameters.plot){
    plot_plane(world,Vpsi,"Vpsi");
    plot_plane(world,Vpsi1,"Vpsi1");
    plot_plane(world,Vpsi2,"Vpsi2");
    }

    return Vpsi;

  }

  std::vector<vecfuncT> TDHF::make_tdhf_potentials(std::vector<CC_vecfunction> &x,const std::vector<CC_vecfunction> &y)const{
    MADNESS_EXCEPTION("NOT IMPLEMENTED",1);
  }



  void TDHF::orthonormalize(std::vector<CC_vecfunction> &x,std::vector<vecfuncT> &V)const{
    CC_Timer time(world,"Orthonormalization");
    //make the Hamilton matrix for the vectorfunctions
    Tensor<double> F = make_perturbed_fock_matrix(x,V);

    // make the overlap matrix
    Tensor<double> S = make_overlap_matrix(x);

    // Diagonalize the F Matrix
    Tensor<double> U, evals;
    Tensor<double> dummy(x.size());
    U = nemo.get_calc() -> get_fock_transformation(world, S, F, evals, dummy, 2.0*parameters.thresh_3D);

    // Transform the states
    x = transform(x,U);

    // Transform the potentials (if V is empty nothing will happen)
    V = transform(V,U);

    // assign eigenvalues
    for(size_t i=0;i<x.size();i++) x[i].omega = evals(i);
    time.info();
  }

  std::vector<CC_vecfunction> TDHF::transform(const std::vector<CC_vecfunction> &x,const madness::Tensor<double> U) const {
    std::vector<CC_vecfunction> transformed;
    for(size_t k=0;k<x.size();k++){
      vecfuncT new_x = zero_functions_compressed<double,3>(world,x[k].size());
      compress(world,x[k].get_vecfunction());
      for(size_t l=0;l<x.size();l++){
	gaxpy(world,1.0,new_x,U(l,k),x[l].get_vecfunction()); // gaxpy(alpha,a,beta,b) -> a[i]=alpha*a[i] + beta*b[i], since there is no += for vectorfunctions implemented
      }
      CC_vecfunction tmp(new_x,x[k].type,parameters.freeze);
      tmp.omega=x[k].omega;
      transformed.push_back(tmp);
    }
    MADNESS_ASSERT(transformed.size()==x.size());
    return transformed;

  }

  Tensor<double> TDHF::make_overlap_matrix(const std::vector<CC_vecfunction> &x)const{
    CC_Timer time(world,"Make Overlap Matrix");
    Tensor<double> S(x.size(),x.size());
    for(size_t k=0;k<x.size();k++){
      const vecfuncT kbra = make_bra(x[k]);
      for(size_t l=0;l<x.size();l++){
	S(l,k) = inner(world,kbra,x[l].get_vecfunction()).sum();
      }
    }
    time.info(parameters.debug);
    if(parameters.debug and world.rank()==0) std::cout << std::fixed << std::setprecision(5) << "\nOverlap Matrix\n" << S << "\n";
    return S;
  }

  Tensor<double> TDHF::make_perturbed_fock_matrix(const std::vector<CC_vecfunction> &x, const std::vector<vecfuncT> &V)const{
    // Make formated timings
    CC_Timer timeF(world,"Matrix: F");
    CC_Timer timeT(world,"Matrix: T");
    CC_Timer timeV(world,"Matrix: V");
    CC_Timer timeR(world,"Matrix: e");

    // bra elements of x
    std::vector<vecfuncT> xbra;

    {
      CC_Timer time_bra(world,"Make bra elements");
      for(size_t k=0;k<x.size();k++){
	const vecfuncT xbrak = make_bra(x[k]);
	xbra.push_back(xbrak);
      }
      MADNESS_ASSERT(xbra.size()==x.size());
      time_bra.info(parameters.debug);
    }

    timeF.start();
    Tensor<double> F(x.size(),x.size());
    {
      Tensor<double> T(x.size(),x.size());
      {
	timeT.start();
	// gradient operator
	std::vector < std::shared_ptr<real_derivative_3d> > D =gradient_operator<double, 3>(world);
	for(size_t k=0;k<x.size();k++){
	  const vecfuncT dkx = apply(world,*(D[0]),xbra[k]);
	  const vecfuncT dky = apply(world,*(D[1]),xbra[k]);
	  const vecfuncT dkz = apply(world,*(D[2]),xbra[k]);
	  for(size_t l=0;l<x.size();l++){
	    const vecfuncT dlx = apply(world,*(D[0]),x[l].get_vecfunction());
	    const vecfuncT dly = apply(world,*(D[1]),x[l].get_vecfunction());
	    const vecfuncT dlz = apply(world,*(D[2]),x[l].get_vecfunction());
	    T(l,k) = 0.0;
	    T(l,k) +=0.5*inner(world,dlx,dkx).sum();
	    T(l,k) +=0.5*inner(world,dly,dky).sum();
	    T(l,k) +=0.5*inner(world,dlz,dkz).sum();
	  }
	}
	timeT.stop();
      }
      Tensor<double> MV(x.size(),x.size());
      {
	timeV.start();
	bool recompute_V = V.empty();
	for(size_t k=0;k<x.size();k++){
	  vecfuncT Vk;
	  //if(recompute_V) Vk = CCOPS.get_CIS_potential(x[k]);
	  if(recompute_V){
	    msg.output("Recompute V");
	    Vk = get_tda_potential(x[k]);
	  }
	  else Vk = V[k];
	  for(size_t l=0;l<x.size();l++){
	    MV(l,k) = inner(world,xbra[l],Vk).sum();
	  }
	}
	timeV.stop();
      }

      // now set the fock matrix together: F(l,k) = T(l,k) + MV(l,k) - eps(l,k)
      // with eps(l,k) = \sum_i eps_i <xl_i|xk_i>
      // first get active eps
      timeR.start();
      std::vector<double> eps;
      for(size_t i=0;i<mo_ket_.size();i++) eps.push_back(get_orbital_energy(i));
      std::vector<double> active_eps;
      for(size_t i=parameters.freeze;i<eps.size();i++) active_eps.push_back(eps[i]);
      for(size_t k=0;k<x.size();k++){
	for(size_t l=0;l<x.size();l++){
	  Tensor<double> xlk = inner(world,xbra[l],x[k].get_vecfunction());
	  MADNESS_ASSERT(xlk.size()==active_eps.size());
	  double eps_part =0.0;
	  for(size_t i=0;i<active_eps.size();i++) eps_part += xlk(i)*active_eps[i];
	  F(l,k) = T(l,k) + MV(l,k) - eps_part;
	}
      }
      timeR.stop();
      if(parameters.debug and world.rank()==0) std::cout << std::fixed << std::setprecision(5) << "\nKinetik Matrix\n" << T << "\n";
      if(parameters.debug and world.rank()==0) std::cout << std::fixed << std::setprecision(5) << "\nPotential Matrix\n" << MV << "\n";
      if(parameters.debug and world.rank()==0) std::cout << std::fixed << std::setprecision(5) << "\nPerturbed Fock Matrix\n" << F << "\n";
    }
    timeF.stop();
    if(parameters.debug and world.rank()==0) std::cout << std::fixed << std::setprecision(5) << "\nPerturbed Fock Matrix\n" << F << "\n";
    //formated timings output
    if(parameters.debug)timeT.print();
    if(parameters.debug)timeV.print();
    if(parameters.debug)timeR.print();
    timeF.print();

    return F;
  }

  /// Makes the guess for the y-states from empty y states
  std::vector<CC_vecfunction> TDHF::make_y_guess(const std::vector<CC_vecfunction> & x, std::vector<CC_vecfunction> & y)const{
    std::vector<vecfuncT> V;
    std::vector<CC_vecfunction> result;
    MADNESS_EXCEPTION("Not Implemented",1);
    return result;
  }

  /// Makes the guess functions by exciting active orbitals with excitation operators
  std::vector<CC_vecfunction> TDHF::make_guess()const{
    CC_Timer time(world,"Making Guess Functions: " + parameters.tda_guess);
    std::vector<std::string> exop_strings;
    if(parameters.tda_guess=="custom"){
      exop_strings = parameters.tda_exops;
      if(world.rank()==0){
	std::cout << "Custom Excitation Operators Demanded:\n";
	std::cout << exop_strings << "\n";
      }
    }
    else exop_strings = make_predefined_guess_strings(parameters.tda_guess);

    // make the excitation operators
    vecfuncT exops;
    for(const auto& exs:exop_strings){
      std::shared_ptr<FunctionFunctorInterface<double, 3> > exop_functor(new polynomial_functor(exs));
      real_function_3d exop = real_factory_3d(world).functor(exop_functor);
      const double norm =exop.norm2();
      exop=exop.scale(1.0/norm);
      exop.truncate();

      exops.push_back(exop);
    }

    // Excite the last N unfrozen MOs
    size_t N = parameters.tda_guess_orbitals;
    const CC_vecfunction & mos = get_active_mo_ket();
    // if N was not assigned we use just the Homo if there are no degeneracies
    // here we check for degeneracies
    if(N==0){
      const Tensor<double>& eps= nemo.get_calc()->aeps;
      const double homo = eps(mo_ket_.size()-1);
      for(size_t i=parameters.freeze;i<mo_ket_.size();i++){
	if(fabs(eps(i)-homo)<parameters.thresh_3D*10.0) N++; // happens at least once for the homo itself
      }
    }

    // making the guess
    std::vector<CC_vecfunction> guess;
    for(size_t i=0;i<exops.size();i++){
      const vecfuncT vm = mos.get_vecfunction();
      reconstruct(world,vm);
      reconstruct(world,exops);
      MADNESS_ASSERT(not(N>vm.size()));
      vecfuncT tmp= zero_functions<double,3>(world,vm.size());
      // exciting the first N orbitals (from the homo to the homo-N)
      for(size_t k=0;k<N;k++){
	real_function_3d xmo = (exops[i]*vm[vm.size()-1-k]).truncate();
	tmp[tmp.size()-1-k]=xmo;
	plot_plane(world,xmo,std::to_string(i)+"_cis_guess_"+"_"+std::to_string(vm.size()-k-1+parameters.freeze));
      }
      tmp = Q(tmp);
      const double norm = norm2(world,tmp);
      scale(world,tmp,1.0/norm);
      CC_vecfunction guess_tmp(tmp,RESPONSE,parameters.freeze);
      guess.push_back(guess_tmp);
    }

    time.info();
    return guess;
  }

  std::vector<CC_vecfunction> TDHF::make_homo_guess()const{
    std::vector<CC_vecfunction> guess_basis = make_guess();
    std::vector<CC_vecfunction> homo_guess;
    const size_t N = parameters.tda_guess_orbitals;
    const size_t M = mo_ket_.size()-parameters.freeze;
    MADNESS_ASSERT(N>0);

    vecfuncT basis;
    for(size_t i=0;i<guess_basis.size();i++){
      const vecfuncT& vtmp = guess_basis[i].get_vecfunction();
      for(size_t k=0;k<vtmp.size();k++){
	if(vtmp[k].norm2()>parameters.thresh_3D) basis.push_back(vtmp[k]);
      }
    }
    if(world.rank()==0) std::cout << "Created Guess Basis of " << basis.size() << " Orbitals\n";

    // now use every of the created basis functions as guess for the homo-response
    // if we have degenerate homo then distribute all combinations to homo and homo-1
    bool degenerate = true; // introduce this if this works parameters.tda_degenerate;

    if(degenerate){
      if(world.rank()==0) std::cout << "Degenerate Homo ... using Homo and Homo-1 for Homo-Guess\n";
      for(size_t i=0;i<basis.size();i++){
	vecfuncT tmp1=zero_functions<double,3>(world,2);
	vecfuncT tmp2=zero_functions<double,3>(world,2);
	tmp1[0]=basis[i];
	tmp2[1]=basis[i];
	CC_vecfunction tmp11(tmp1,RESPONSE,parameters.freeze+M-2);
	CC_vecfunction tmp22(tmp2,RESPONSE,parameters.freeze+M-2);
	homo_guess.push_back(tmp11);
	homo_guess.push_back(tmp22);

      }
    }else{
      for(size_t i=0;i<basis.size();i++){
	vecfuncT tmp;
	tmp.push_back(basis[i]);
	CC_vecfunction tmp2(tmp,RESPONSE,parameters.freeze+M-1);
	homo_guess.push_back(tmp2);
      }
    }

    if(world.rank()==0) std::cout << "Created " << homo_guess.size() << " Homo-Guess Functions\n";

    std::vector<vecfuncT> dummy;
    //orthonormalize_cholesky(homo_guess,dummy);
    orthonormalize(homo_guess,dummy);
    std::sort(homo_guess.begin(),homo_guess.end());
    if(world.rank()==0) std::cout << "Homo-Guess Functions after Orthonormalization\n";
    for(const auto& x:homo_guess) if(world.rank()==0) std::cout << "omega=" <<  x.omega << ", ||f||=" << norm2(world,x.get_vecfunction()) << "\n";

    // Now pack them back to original size (all others just zero functions)
    std::vector<CC_vecfunction> full_guess;
    for(size_t i=0;i<homo_guess.size();i++){
      vecfuncT tmp = zero_functions<double,3>(world,M);
      size_t hs=homo_guess[i].get_vecfunction().size();
      for(size_t k=0;k<hs;k++){
	tmp[M-hs+k]=homo_guess[i].get_vecfunction()[k];
      }
      CC_vecfunction tmp2(tmp,RESPONSE,parameters.freeze);
      full_guess.push_back(tmp2);
    }
    return full_guess;

  }

  bool TDHF::initialize_singles(CC_vecfunction &singles,const functype type,const int ex) const {
    MADNESS_ASSERT(singles.size()==0);
    bool restarted = false;

    std::vector<CC_function> vs;
    for(size_t i=parameters.freeze;i<mo_ket_.size();i++){
      CC_function single_i;
      single_i.type=type;
      single_i.i = i;
      std::string name;
      if(ex<0) name = single_i.name();
      else name = std::to_string(ex)+"_"+single_i.name();
      real_function_3d tmpi = real_factory_3d(world);
      const bool found=load_function<double,3>(tmpi,name);
      if(found) restarted = true;
      else msg.output("Initialized " + single_i.name()+" of type " + assign_name(type) +" as zero-function");
      single_i.function = copy(tmpi);
      vs.push_back(single_i);
    }

    singles = CC_vecfunction(vs,type);
    if(type==RESPONSE) singles.excitation=ex;

    return restarted;
  }

  /// Makes an automated excitation operator string for the excitation operators needed in the guess
  std::vector<std::string> TDHF::make_auto_polynom_guess(const size_t order)const{
    std::vector<std::string> exop_strings;
    for(size_t i=0; i<order+1; i++){
      for(size_t j=0; j<order+1 ; j++){
	for(size_t k=0;k<order+1 ; k++){
	  if(i+j+k > order) ; // do nothing
	  else if(i==0 and j==0 and k==0) ; // do nothing
	  else{
	    if(i==0 and j!=0 and k!=0) exop_strings.push_back(" y " + madness::stringify(j) + " z " + madness::stringify(k) );
	    else if(j==0 and i!=0 and k!=0) exop_strings.push_back("x " + madness::stringify(i) + " z " + madness::stringify(k) );
	    else if(k==0 and i!=0 and j!=0) exop_strings.push_back("x " + madness::stringify(i) + " y " + madness::stringify(j));
	    else if(i==0 and j==0) exop_strings.push_back(" z " + madness::stringify(k) );
	    else if(i==0 and k==0) exop_strings.push_back(" y " + madness::stringify(j));
	    else if(j==0 and k==0) exop_strings.push_back("x " + madness::stringify(i));
	    else exop_strings.push_back("x " + madness::stringify(i) + " y " + madness::stringify(j) + " z " + madness::stringify(k) );
	  }
	}
      }
    }
    return exop_strings;
  }
  /// Makes an excitation operator string based on predefined keywords
  std::vector<std::string> TDHF::make_predefined_guess_strings(const std::string what)const{
    std::vector<std::string> exop_strings;
    if(what == "dipole"){
      exop_strings.resize(3);
      exop_strings[0] = "x 1.0";
      exop_strings[1] = "y 1.0";
      exop_strings[2] = "z 1.0";
    }else if(what == "x"){
      exop_strings.resize(1);
      exop_strings[0] = "x 1.0";
    }else if(what == "y"){
      exop_strings.resize(1);
      exop_strings[0] = "y 1.0";
    }else if(what == "z"){
      exop_strings.resize(1);
      exop_strings[0] = "z 1.0";
    }else if(what == "r2"){
      exop_strings.resize(1);
      exop_strings[0] = "x 2.0 , y 2.0 , z 2.0";
    }else if(what == "quadrupole"){
      exop_strings.resize(9);
      exop_strings[0] = "x 1.0";
      exop_strings[1] = "y 1.0";
      exop_strings[2] = "z 1.0";
      exop_strings[3] = "x 2.0";
      exop_strings[4] = "y 2.0";
      exop_strings[5] = "z 2.0";
      exop_strings[6] = "x 1.0 y 1.0";
      exop_strings[7] = "x 1.0 z 1.0";
      exop_strings[8] = "y 1.0 z 1.0";
    }else if(what == "dipole+"){
      exop_strings.resize(4);
      exop_strings[0] = "x 1.0";
      exop_strings[1] = "y 1.0";
      exop_strings[2] = "z 1.0";
      exop_strings[3] = "x 2.0 , y 2.0 , z 2.0";
    }else if(what == "dipole+diffuse"){
      exop_strings.resize(4);
      exop_strings[0] = "x 3.0 , x 1.0 y 2.0 , x 1.0 z 2.0";
      exop_strings[1] = "x 2.0 y 1.0 , y 3.0 , y 1.0 z 2.0";
      exop_strings[2] = "x 2.0 z 1.0 , y 2.0 z 1.0 , z 3.0";
      exop_strings[3] = "x 4.0 , y 4.0 , z 4.0, x 2.0 y 2.0, x 2.0 z 2.0, y 2.0 z 2.0";
    }else if(what == "dipole+diffuse_big"){
      exop_strings.resize(8);
      exop_strings[0] = "x 1.0";
      exop_strings[1] = "y 1.0";
      exop_strings[2] = "z 1.0";
      exop_strings[3] = "x 2.0 , y 2.0 , z 2.0";
      exop_strings[4] = "x 3.0 , x 1.0 y 2.0 , x 1.0 z 2.0";
      exop_strings[5] = "x 2.0 y 1.0 , y 3.0 ,y 1.0 z 2.0";
      exop_strings[6] = "x 2.0 z 1.0 , y 2.0 z 1.0 , z 3.0";
      exop_strings[7] = "x 4.0 , 4 2.0 , 4 2.0, x 2.0 y 2.0, x 2.0 z 2.0, y 2.0 z 2.0";
    }else if(what == "c2v"){
      exop_strings.resize(4);
      exop_strings[0] = "z 1.0 , z 3.0 , x 2.0 z 1.0 , y 2.0 z 1.0 , x 2.0 , y 2.0 , z 2.0 , x 4.0 , y 4.0 , z 4.0 , x 2.0 y 2.0 , x 2.0 z 2.0 , y 2.0 z 2.0";
      exop_strings[1] = "x 1.0 y 1.0 , x 3.0 y 1.0 , x 1.0 y 3.0 , x 1.0 y 1.0 z 1.0 , x 1.0 y 1.0 z 2.0";
      exop_strings[2] = "x 1.0 , x 1.0 z 1.0 , x 1.0 z 2.0 , x 3.0 , x 3.0 z 1.0 , x 1.0 z 3.0 , x 1.0 y 2.0 , x 1.0 y 2.0 z 1.0";
      exop_strings[3] = "y 1.0 , y 1.0 z 1.0 , y 1.0 z 2.0 , y 3.0 z 1.0 , y 1.0 z 3.0 , y 3.0, x 2.0 y 1.0 , x 2.0 y 1.0 z 1.0 ";
    }else if(what == "water_first"){
      exop_strings.resize(1);
      exop_strings[0] = "x 1.0 y 1.0, x 1.0 y 1.0 z 1.0";
    }else if(what == "c2v_big"){
      exop_strings.resize(8);
      exop_strings[0] = "z 1.0 , z 3.0 , x 2.0 z 1.0 , y 2.0 z 1.0";
      exop_strings[1] = "x 2.0 , y 2.0 , z 2.0 , x 4.0 , y 4.0 , z 4.0 , x 2.0 y 2.0 , x 2.0 z 2.0 , y 2.0 z 2.0";
      exop_strings[2] = "x 1.0 y 1.0 , x 3.0 y 1.0 , x 1.0 y 3.0";
      exop_strings[3] = "x 1.0 y 1.0 z 1.0 , x 1.0 y 1.0 z 2.0";
      exop_strings[4] = "x 1.0 , x 1.0 z 1.0 , x 1.0 z 2.0 , x 3.0 , x 3.0 z 1.0 , x 1.0 z 3.0";
      exop_strings[5] = "x 1.0 y 2.0 , x 1.0 y 2.0 z 1.0";
      exop_strings[6] = "y 1.0 , y 1.0 z 1.0 , y 1.0 z 2.0 , y 3.0 z 1.0 , y 1.0 z 3.0 , y 3.0";
      exop_strings[7] = "x 2.0 y 1.0 , x 2.0 y 1.0 z 1.0";
    }else if(what == "big_fock"){exop_strings = make_auto_polynom_guess(6);
    }else if(what == "small_fock"){exop_strings = make_auto_polynom_guess(4);
    }else if(what == "big_fock_2"){exop_strings = make_auto_polynom_guess(2);
    }else if(what == "big_fock_3"){exop_strings = make_auto_polynom_guess(3);
    }else if(what == "big_fock_4"){exop_strings = make_auto_polynom_guess(4);
    }else if(what == "big_fock_5"){exop_strings = make_auto_polynom_guess(5);
    }else if(what == "big_fock_6"){exop_strings = make_auto_polynom_guess(6);
    }else if(what == "big_fock_7"){exop_strings = make_auto_polynom_guess(7);
    }else if(what == "big_fock_8"){exop_strings = make_auto_polynom_guess(8);
    }else if(what == "big_fock_9"){exop_strings = make_auto_polynom_guess(9);
    }else std::cout << "Keyword " << what << " is not known" << std::endl;
    return exop_strings;
  }



  TDHF::~TDHF() {
    // TODO Auto-generated destructor stub
  }

} /* namespace madness */
