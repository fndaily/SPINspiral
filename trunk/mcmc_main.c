// mcmc_main.c
// Main routine of spinning MCMC code
// v.10: Include correlated jump proposals; the chain now starts with a guess for a diagonal covariance matrix
// v.11: Include chain temperature and offset runs
// v.12: Test off-set run and degeneracy between sky position and distance
// v.13: Introduce parallel tempering



#include <mcmc.h>


// Main program:
int main(int argc, char * argv[])
{
  // Interferometers are managed via the `database'; the `network' is a vector of pointers to the database (see below).
  // The interferometers that are actually used need to be initialised via the `ifoinit()'-function in order to determine noise PSD, signal FT &c.
  
  printf("\n\n   Starting MCMC code...\n");
  int i;
  double snr;
  
  
  //Initialise stuff for the run
  struct runpar run;
  sprintf(run.infilename,"mcmc.input"); //Default input filename
  if(argc > 1) sprintf(run.infilename,argv[1]);
  
  setconstants(&run);    //Set the global constants (which are variable in C). This routine should eventuelly disappear.
  readlocalfile();       //Read system-dependent data, e.g. path to data files
  readinputfile(&run);   //Read data for this run from input.mcmc
  setmcmcseed(&run);     //Set mcmcseed (if 0), or use the current value
  writeinputfile(&run);  //Write run data to nicely formatted input.mcmc.<mcmcseed>
  
  //Set up the data of the IFOs you may want to use (H1,L1 + VIRGO by default)
  struct interferometer database[3];
  set_ifo_data(run, database);  
  
  //Define interferometer network which IFOs.  The first run.networksize are actually used
  struct interferometer *network[3] = {&database[0], &database[1], &database[2]};
  int networksize = run.networksize;
  
  //Initialise interferometers, read and prepare data, inject signal (takes some time)
  if(networksize == 1) {
    printf("   Initialising 1 IFO, reading data...\n");
  } else {
    printf("   Initialising %d IFOs, reading datafiles...\n",networksize);
  }
  ifoinit(network, networksize);
  if(inject) {
    if(run.targetsnr < 0.001) printf("   A signal with the 'true' parameter values was injected.\n");
  } else {
    printf("   No signal was injected.\n");
  }
  
  
  //Get a parameter set to calculate SNR or write the wavefrom to disc
  struct parset dummypar;
  gettrueparameters(&dummypar);
  dummypar.loctc    = (double*)calloc(networksize,sizeof(double));
  dummypar.localti  = (double*)calloc(networksize,sizeof(double));
  dummypar.locazi   = (double*)calloc(networksize,sizeof(double));
  dummypar.locpolar = (double*)calloc(networksize,sizeof(double));
  localpar(&dummypar, network, networksize);
  
  
  //Calculate SNR
  run.netsnr = 0.0;
  if(dosnr==1) {
    for (i=0; i<networksize; ++i) {
      snr = signaltonoiseratio(&dummypar, network, i);
      network[i]->snr = snr;
      run.netsnr += snr*snr;
    }
    run.netsnr = sqrt(run.netsnr);
  }
  
  //Get the desired SNR by scaling the distance
  if(run.targetsnr > 0.001 && inject==1) {
    //run.targetsnr = 17.0;  //Target SNR for the network
    truepar[3] *= (run.netsnr/run.targetsnr);  //Use total network SNR
    //truepar[3] *= (run.netsnr/(run.targetsnr*sqrt((double)networksize)));  //Use geometric average SNR
    printf("   Setting distance to %lf Mpc to get a network SNR of %lf.\n",truepar[3],run.targetsnr);
    gettrueparameters(&dummypar);
    dummypar.loctc    = (double*)calloc(networksize,sizeof(double));
    dummypar.localti  = (double*)calloc(networksize,sizeof(double));
    dummypar.locazi   = (double*)calloc(networksize,sizeof(double));
    dummypar.locpolar = (double*)calloc(networksize,sizeof(double));
    localpar(&dummypar, network, networksize);
    
    //Recalculate SNR
    run.netsnr = 0.0;
    if(dosnr==1) {
      for (i=0; i<networksize; ++i) {
	snr = signaltonoiseratio(&dummypar, network, i);
	network[i]->snr = snr;
	run.netsnr += snr*snr;
      }
      run.netsnr = sqrt(run.netsnr);
    }
    
    for (i=0; i<networksize; ++i)
      ifodispose(network[i]);
    //Reinitialise interferometers, read and prepare data, inject signal (takes some time)
    if(networksize == 1) {
      printf("   Reinitialising 1 IFO, reading data...\n");
    } else {
      printf("   Reinitialising %d IFOs, reading datafiles...\n",networksize);
    }
    ifoinit(network, networksize);
    printf("   A signal with the 'true' parameter values was injected.\n");
  }
  
  
  
  
  
  
  //Calculate 'null-likelihood'
  struct parset nullpar;
  getnullparameters(&nullpar);
  nullpar.loctc    = (double*)calloc(networksize,sizeof(double));
  nullpar.localti  = (double*)calloc(networksize,sizeof(double));
  nullpar.locazi   = (double*)calloc(networksize,sizeof(double));
  nullpar.locpolar = (double*)calloc(networksize,sizeof(double));
  localpar(&nullpar, network, networksize);
  run.logL0 = net_loglikelihood(&nullpar, networksize, network);
  if(inject == 0) run.logL0 *= 1.01;  //If no signal is injected, presumably there is one present in the data; enlarge the range that log(L) can take by owering Lo (since L>Lo is forced)
  
  
  
  
  //Write the signal and its FFT to disc
  if(writesignal) {
    for (i=0; i<networksize; ++i) {
      //printmuch=1;
      writesignaltodisc(&dummypar, network, i);
      //printmuch=0;
    }
  }
  writesignal=0;
  
  //Do MCMC
  if(domcmc==1) {
    //printmuch=1;
    mcmc(&run, network);
    //printmuch=0;
  } else {
    printf("%10s  %10s  %6s  %20s  %6s  ","niter","nburn","seed","null likelihood","ndet");
    for(i=0;i<networksize;i++) {
      printf("%16s%4s  ",network[i]->name,"SNR");
    }
    printf("\n%10d  %10d  %6d  %20.10lf  %6d  ",iter,nburn,run.mcmcseed,run.logL0,networksize);
    for(i=0;i<networksize;i++) {
      printf("%20.10lf  ",network[i]->snr);
    }
    printf("\n\n%8s  %8s  %17s  %8s  %8s  %8s  %8s  %8s  %8s  %8s  %8s  %8s\n",
	   "Mc","eta","tc","logdL","sinlati","longi","phase","spin","kappa","sinthJ0","phiJ0","alpha");
    printf("%8.5f  %8.5f  %17.6lf  %8.5f  %8.5f  %8.5f  %8.5f  %8.5f  %8.5f  %8.5f  %8.5f  %8.5f\n\n",
	   dummypar.mc,dummypar.eta,dummypar.tc,dummypar.logdl,dummypar.sinlati,dummypar.longi,dummypar.phase,dummypar.spin,dummypar.kappa,dummypar.sinthJ0,dummypar.phiJ0,dummypar.alpha);
  }
  
  //Calculate matches between two signals
  if(domatch==1) {
    printf("\n");
    gettrueparameters(&dummypar);
    dummypar.loctc    = (double*)calloc(networksize,sizeof(double));
    dummypar.localti  = (double*)calloc(networksize,sizeof(double));
    dummypar.locazi   = (double*)calloc(networksize,sizeof(double));
    dummypar.locpolar = (double*)calloc(networksize,sizeof(double));
    
    FILE *fout;
    fout = fopen("tc.dat","w");
    double fac=0.0;
    double matchpar = dummypar.tc,matchres=0.0;
    for(fac=-0.002;fac<0.002;fac+=0.00005) {
      dummypar.tc = matchpar+fac;
      for(i=0;i<networksize;i++) {
        localpar(&dummypar, network, networksize);
        matchres = match(&dummypar,network,i,networksize);
        printf("%10.6f  %10.6f\n",fac,matchres);
        fprintf(fout,"%10.6f  %10.6f\n",fac,matchres);
      }
    }
    fclose(fout);
  }
  
  
  //Get rid of allocated memory and quit
  for (i=0; i<networksize; ++i)
    ifodispose(network[i]);
  
  
  free(nullpar.loctc);
  free(nullpar.localti);
  free(nullpar.locazi);
  free(nullpar.locpolar);
  
  free(dummypar.loctc);
  free(dummypar.localti);
  free(dummypar.locazi);
  free(dummypar.locpolar);
  
  printf("\n   MCMC code done.\n\n\n");
  return 0;
}







//Deallocate the struct parset
void pardispose(struct parset *par)
{
  free(par->loctc);         par->loctc        = NULL;
  free(par->localti);       par->localti      = NULL;
  free(par->locazi);        par->locazi       = NULL;
  free(par->locpolar);      par->locpolar     = NULL;
}




void setmcmcseed(struct runpar *run)
//If run->mcmcseed==0, set it using the system clock
{
  struct timeval time;
  struct timezone tz;
  gettimeofday(&time, &tz);
  if(run->mcmcseed==0) run->mcmcseed = time.tv_usec;
}







