// This code implementation is the intellectual property of
// the GEANT4 collaboration.
//
// By copying, distributing or modifying the Program (or any work
// based on the Program) you indicate your acceptance of this statement,
// and all its terms.
//
// $Id: G4LowEnergyGammaConversion.cc,v 1.9.8.1 1999/12/07 20:50:24 gunter Exp $
// GEANT4 tag $Name: geant4-01-00 $
//
// 
// --------------------------------------------------------------
//      GEANT 4 class implementation file
//      CERN Geneva Switzerland
//
//      For information related to this code contact:
//      CERN, IT Division, ASD group
//      History: first implementation, based on object model of
//      2nd December 1995, G.Cosmo
//      ------------ G4LowEnergyGammaConversion physics process --------
//                   by Michel Maire, 24 May 1996
// **************************************************************
// 11-06-96, Added SelectRandomAtom() method, M.Maire
// 21-06-96, SetCuts implementation, M.Maire
// 24-06-96, simplification in ComputeMicroscopicCrossSection, M.Maire
// 24-06-96, in DoIt : change the particleType stuff, M.Maire
// 25-06-96, modification in the generation of the teta angle, M.Maire
// 16-09-96, minors optimisations in DoIt. Thanks to P.Urban
//           dynamical array PartialSumSigma
// 13-12-96, fast sampling of epsil below 2 MeV, L.Urban
// 14-01-97, crossection table + meanfreepath table.
//           PartialSumSigma removed, M.Maire
// 14-01-97, in DoIt the positron is always created, even with Ekine=0,
//           for further annihilation, M.Maire
// 14-03-97, new Physics scheme for geant4alpha, M.Maire
// 28-03-97, protection in BuildPhysicsTable, M.Maire
// 19-06-97, correction in ComputeMicroscopicCrossSection, L.Urban
// 04-06-98, in DoIt, secondary production condition: range>min(threshold,safety)
// --------------------------------------------------------------

// This Class Header
#include "G4LowEnergyGammaConversion.hh"

// Collaborating Class Headers
#include "G4EnergyLossTables.hh"
#include "G4Electron.hh"
#include "G4Positron.hh"

// constructor
 
G4LowEnergyGammaConversion::G4LowEnergyGammaConversion(const G4String& processName)
  : G4VDiscreteProcess(processName),   
    theCrossSectionTable(0),
    theMeanFreePathTable(0),
    ZNumVec(0),
    LowestEnergyLimit (1.2200),
    HighestEnergyLimit(100*GeV),
    NumbBinTable(200)
{
   if (verboseLevel>0) {
     G4cout << GetProcessName() << " is created "<< endl;
     G4cout << "LowestEnergy: " << LowestEnergyLimit/keV << "keV ";
     G4cout << "HighestEnergy: " << HighestEnergyLimit/GeV << "GeV " << endl;
   }
}
 
// destructor
 
G4LowEnergyGammaConversion::~G4LowEnergyGammaConversion()
{
   if (theCrossSectionTable) {

      delete theCrossSectionTable;
   }

   if (theMeanFreePathTable) {
      theMeanFreePathTable->clearAndDestroy();
      delete theMeanFreePathTable;
   }

   if(ZNumVec){
     
     ZNumVec->clear();
     delete ZNumVec;
   }
}
 
 
// methods.............................................................................
void G4LowEnergyGammaConversion::BuildPhysicsTable(const G4ParticleDefinition& GammaType){

  BuildZVec();

  // Build microscopic cross section tables for the Compton Scattering process
  BuildCrossSectionTable();

  // Build mean free path table for the Compton Scattering process
  BuildMeanFreePathTable();
}

void G4LowEnergyGammaConversion::BuildCrossSectionTable(){
 
  if (theCrossSectionTable) {
    
    delete theCrossSectionTable; 
  }

  theCrossSectionTable = new G4SecondLevel();
  G4int dataNum = 2;
 
  for(G4int TableInd = 0; TableInd < ZNumVec->entries(); TableInd++){

    G4int AtomInd = (G4int) (*ZNumVec)[TableInd];

    G4FirstLevel* oneAtomCS = util.BuildFirstLevelTables(AtomInd, dataNum, "pair/pp-cs-");
     
     theCrossSectionTable->insert(oneAtomCS);
   
  }//end for on atoms
}

void G4LowEnergyGammaConversion::BuildZVec(){

  const G4MaterialTable* theMaterialTable=G4Material::GetMaterialTable();
  G4int numOfMaterials = theMaterialTable->length();

  if(ZNumVec){

    ZNumVec->clear();
    delete ZNumVec;
  }
  
  ZNumVec = new G4Data(); 
  for (G4int J=0 ; J < numOfMaterials; J++){ 
    
    const G4Material* material= (*theMaterialTable)[J];        
    const G4ElementVector* theElementVector = material->GetElementVector();
    const G4int NumberOfElements = material->GetNumberOfElements() ;

    for (G4int iel=0; iel<NumberOfElements; iel++ ){
      
      G4double Zel = (*theElementVector)(iel)->GetZ();
      
      if(ZNumVec->contains(Zel) == FALSE){
	
	ZNumVec->insert(Zel);
      }
      else{
	
	continue;
      }
    }
  }
}


G4VParticleChange* G4LowEnergyGammaConversion::PostStepDoIt(const G4Track& aTrack, const G4Step&  aStep){

//
// The secondaries e+e- energies are sampled using the Bethe - Heitler 
// cross sections with Coulomb correction. A modified version of the random 
// number techniques of Butcher & Messel is used (Nuc Phys 20(1960),15).
//
// GEANT4 internal units.
//
// Note 1 : Effects due to the breakdown of the Born approximation at low 
// energy are ignored.
// Note 2 : The differential cross section implicitly takes account of 
// pair creation in both nuclear and atomic electron fields. However triplet 
// prodution is not generated.

  aParticleChange.Initialize(aTrack);

  G4Material* aMaterial = aTrack.GetMaterial();
  
  const G4DynamicParticle* aDynamicGamma = aTrack.GetDynamicParticle();
  G4double GammaEnergy = aDynamicGamma->GetKineticEnergy();
  G4ParticleMomentum GammaDirection = aDynamicGamma->GetMomentumDirection();
  
  G4double epsil ;
  G4double epsil0 = electron_mass_c2 /  GammaEnergy ;

  // do it fast if GammaEnergy < 2. MeV
  const G4double Egsmall=2.*MeV; 
  if (GammaEnergy<Egsmall) { epsil = epsil0 + (0.5-epsil0)*G4UniformRand(); }
  
  else{  // now comes the case with GammaEnergy >= 2. MeV
      
    // select randomly one element constituing the material  
    G4Element* anElement = SelectRandomAtom(aDynamicGamma, aMaterial);
    
    // Extract Coulomb factor for this Element
    G4double FZ = 8.*(anElement->GetIonisation()->GetlogZ3());
    if (GammaEnergy > 50.*MeV) FZ += 8.*(anElement->GetfCoulomb());
   
    // limits of the screening variable
    G4double screenfac = 136.*epsil0/(anElement->GetIonisation()->GetZ3()) ;
    G4double screenmax = exp ((42.24 - FZ)/8.368) - 0.952 ;
    G4double screenmin = min(4.*screenfac,screenmax) ;
    
    // limits of the energy sampling
    G4double epsil1 = 0.5 - 0.5*sqrt(1. - screenmin/screenmax) ;
    G4double epsilmin = max(epsil0,epsil1) , epsilrange = 0.5 - epsilmin ;
    
    //
    // sample the energy rate of the created electron (or positron) 
    //
    //G4double epsil, screenvar, greject ;
    G4double  screenvar, greject ;
    
    G4double F10 = ScreenFunction1(screenmin) - FZ , F20 = ScreenFunction2(screenmin) - FZ;
    G4double NormF1 = max(F10*epsilrange*epsilrange,0.) ,  NormF2 = max(1.5*F20,0.);
    
    do {
      if ( NormF1/(NormF1+NormF2) > G4UniformRand() ){ 

	epsil = 0.5 - epsilrange*pow(G4UniformRand(), 1/3) ;
	screenvar = screenfac/(epsil*(1-epsil));
	greject = (ScreenFunction1(screenvar) - FZ)/F10 ;
      } 
      else { 
	epsil = epsilmin + epsilrange*G4UniformRand();
	screenvar = screenfac/(epsil*(1-epsil));
	greject = (ScreenFunction2(screenvar) - FZ)/F20 ;
      }
      
    } while( greject < G4UniformRand() );
    
  }   //  end of epsil sampling.........................
  
  //
  // fixe charges randomly
  // 
  
  G4double ElectTotEnergy, PositTotEnergy;
  if (RandFlat::shootBit()){

    ElectTotEnergy = (1.-epsil)*GammaEnergy;
    PositTotEnergy = epsil*GammaEnergy;
  }
  else{
    
    PositTotEnergy = (1.-epsil)*GammaEnergy;
    ElectTotEnergy = epsil*GammaEnergy;
  }
  
//
// scattered electron (positron) angles. ( Z - axis along the parent photon)
// universal distribution suggested by L. Urban (Geant3 manual (1993) Phys211),
//  derived from Tsai distribution (Rev Mod Phys 49,421(1977))

  G4double u;
  const G4double a1 = 0.625 , a2 = 3.*a1 , d = 27. ;
  
  if (9./(9.+d) > G4UniformRand()){
    u = - log(G4UniformRand()*G4UniformRand())/a1 ;
  }

  else{
    u = - log(G4UniformRand()*G4UniformRand())/a2 ;
  }
  
  G4double Teta = u*electron_mass_c2/GammaEnergy ;
  G4double Phi  = twopi * G4UniformRand() ;
  G4double dirx = sin(Teta)*cos(Phi) , diry = sin(Teta)*sin(Phi) , dirz = cos(Teta);
  
//
// kinematic of the created pair
// the electron and positron are assumed to have a symetric angular 
// distribution with respect to the Z axis along the parent photon.

  G4double LocalEnerDeposit = 0. ;
  aParticleChange.SetNumberOfSecondaries(2) ; 
  
  G4double ElectKineEnergy = max(0.,ElectTotEnergy - electron_mass_c2) ;

  if (G4EnergyLossTables::GetRange(G4Electron::Electron(), ElectKineEnergy, aMaterial)
      >= min(G4Electron::GetCuts(), aStep.GetPostStepPoint()->GetSafety()) ){

    G4ThreeVector ElectDirection ( dirx, diry, dirz );
    ElectDirection.rotateUz(GammaDirection);   
    
    // create G4DynamicParticle object for the particle1  
    G4DynamicParticle* aParticle1= new G4DynamicParticle (G4Electron::Electron(),ElectDirection, ElectKineEnergy);

    aParticleChange.AddSecondary( aParticle1 ) ; 
  }
  else{ 
    
    LocalEnerDeposit += ElectKineEnergy ; 
  }

// the e+ is always created (even with Ekine=0) for further annihilation.

  G4double PositKineEnergy = max(0.,PositTotEnergy - electron_mass_c2) ;

  if (G4EnergyLossTables::GetRange(G4Positron::Positron(),PositKineEnergy,aMaterial)
        < min(G4Positron::GetCuts(), aStep.GetPostStepPoint()->GetSafety()) ){

    LocalEnerDeposit += PositKineEnergy ;
    PositKineEnergy = 0. ;
  }
  G4ThreeVector PositDirection ( -dirx, -diry, dirz );
  PositDirection.rotateUz(GammaDirection);   
 
  // create G4DynamicParticle object for the particle2 
  G4DynamicParticle* aParticle2= new G4DynamicParticle (G4Positron::Positron(),
							PositDirection, PositKineEnergy);

  aParticleChange.AddSecondary( aParticle2 ) ; 
  
  aParticleChange.SetLocalEnergyDeposit( LocalEnerDeposit ) ;
  
  //
  // Kill the incident photon 
  //
  
  aParticleChange.SetMomentumChange( 0., 0., 0. ) ;
  aParticleChange.SetEnergyChange( 0. ) ; 
  aParticleChange.SetStatusChange( fStopAndKill ) ;
#ifdef G4VERBOSE
  if(verboseLevel > 15){
    G4cout<<"LE Gamma Conversion PostStepDoIt"<<endl;
  }
#endif
  //  Reset NbOfInteractionLengthLeft and return aParticleChange
  return G4VDiscreteProcess::PostStepDoIt( aTrack, aStep );
}

void G4LowEnergyGammaConversion::BuildMeanFreePathTable(){

  if (theMeanFreePathTable) {
    theMeanFreePathTable->clearAndDestroy(); delete theMeanFreePathTable; }

  // material
  G4double NumbOfMaterials = G4Material::GetNumberOfMaterials();
  const G4MaterialTable* theMaterialTable = G4Material::GetMaterialTable() ;
  G4Material* material;

  // MeanFreePath 
  G4double LowEdgeEnergy, Value;
  theMeanFreePathTable = new G4PhysicsTable(NumbOfMaterials);
  G4PhysicsLogVector* ptrVector;

  for ( G4int J = 0 ; J < NumbOfMaterials; J++ ) { // For each material 
  
    //create physics vector then fill it ....
    ptrVector = new  G4PhysicsLogVector(LowestEnergyLimit, HighestEnergyLimit, NumbBinTable);
    
    material = (*theMaterialTable)(J);
    const G4ElementVector* theElementVector = material->GetElementVector();
    const G4double* theAtomNumDensityVector = material->GetAtomicNumDensityVector();   
    
    for ( G4int i = 0 ; i < NumbBinTable ; i++ ){ 
      //For each energy
      
      LowEdgeEnergy = ptrVector->GetLowEdgeEnergy(i);
      
      const G4double BigPath= DBL_MAX;
      G4double SIGMA = 0 ;
      
      for ( G4int k=0 ; k < material->GetNumberOfElements() ; k++ ){ 
	// For each element            
	G4int AtomIndex = (G4int) (*theElementVector)(k)->GetZ();
	const G4FirstLevel* oneAtomCS
	  = (*theCrossSectionTable)[ZNumVec->index(AtomIndex)];

	G4double interCrsSec = util.DataLogInterpolation(LowEdgeEnergy, 
							 (*(*oneAtomCS)[0]), 
							 (*(*oneAtomCS)[1]))*barn;

	SIGMA += theAtomNumDensityVector[k]*interCrsSec;

      }       
      
      Value = SIGMA<=0.0 ? BigPath : 1./SIGMA ;
      
      ptrVector->PutValue( i , Value ) ;
    }
    
    theMeanFreePathTable->insertAt( J , ptrVector ) ;
  }
}

G4Element* G4LowEnergyGammaConversion::SelectRandomAtom(const G4DynamicParticle* aDynamicGamma, G4Material* aMaterial){

  // select randomly 1 element within the material 
  G4double GammaEnergy = aDynamicGamma->GetKineticEnergy();
  const G4int NumberOfElements = aMaterial->GetNumberOfElements();
  const G4ElementVector* theElementVector = aMaterial->GetElementVector();
  if (NumberOfElements == 1) return (*theElementVector)(0);

  const G4double* theAtomNumDensityVector = aMaterial->GetAtomicNumDensityVector();

  G4double PartialSumSigma = 0.;
  G4double rval = G4UniformRand()/MeanFreePath;

  for ( G4int i=0 ; i < NumberOfElements ; i++ ){ 

    G4double crossSection;
    if (GammaEnergy <  LowestEnergyLimit)
      crossSection = 0. ;
    else {
      if (GammaEnergy > HighestEnergyLimit) GammaEnergy = 0.99*HighestEnergyLimit ;

      G4int AtomIndex = (G4int) (*theElementVector)(i)->GetZ();
      const G4FirstLevel* oneAtomCS
	= (*theCrossSectionTable)[ZNumVec->index(AtomIndex)];
      
      crossSection =  util.DataLogInterpolation(GammaEnergy, 
						(*(*oneAtomCS)[0]), 
						(*(*oneAtomCS)[1]))*barn;

    }
    
    PartialSumSigma += theAtomNumDensityVector[i] * crossSection;
    if(rval <= PartialSumSigma) return ((*theElementVector)(i));
  }
  //  G4cout << " WARNING !!! - The Material '"<< aMaterial->GetName()
  //   << "' has no elements" << endl;
  return (*theElementVector)(0);
}


