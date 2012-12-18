/**************************************************************************
 * Copyright(c) 1998-1999, ALICE Experiment at CERN, All rights reserved. *
 *                                                                        *
 * Author: The ALICE Off-line Project.                                    *
 * Contributors are mentioned in the code where appropriate.              *
 *                                                                        *
 * Permission to use, copy, modify and distribute this software and its   *
 * documentation strictly for non-commercial purposes is hereby granted   *
 * without fee, provided that the above copyright notice appears in all   *
 * copies and that both the copyright notice and this permission notice   *
 * appear in the supporting documentation. The authors make no claims     *
 * about the suitability of this software for any purpose. It is          *
 * provided "as is" without express or implied warranty.                  *
 **************************************************************************/

#include "AliMuonTrackCuts.h"

#include "TMath.h"
#include "TList.h"
#include "TArrayD.h"
#include "TArrayI.h"
#include "TObjArray.h"
#include "TObjString.h"
#include "TFile.h"
#include "TParameter.h"
#include "TKey.h"
#include "TVector3.h"
#include "TSystem.h"

#include "AliLog.h"
#include "AliVParticle.h"
#include "AliESDMuonTrack.h"
#include "AliAnalysisManager.h"
#include "AliInputEventHandler.h"
#include "AliVEvent.h"

#include "AliOADBContainer.h"

#include "AliAnalysisMuonUtility.h"

/// \cond CLASSIMP
ClassImp(AliMuonTrackCuts) // Class implementation in ROOT context
/// \endcond


//________________________________________________________________________
AliMuonTrackCuts::AliMuonTrackCuts() :
  AliAnalysisCuts(),
  fIsMC(kFALSE),
  fUseCustomParam(kFALSE),
  fSharpPtCut(kFALSE),
  fAllowDefaultParams(kFALSE),
  fPassNumber(-1),
  fOADBParam()
{
  /// Default ctor.
}

//________________________________________________________________________
AliMuonTrackCuts::AliMuonTrackCuts(const char* name, const char* title ) :
AliAnalysisCuts(name, title),
  fIsMC(kFALSE),
  fUseCustomParam(kFALSE),
  fSharpPtCut(kFALSE),
  fAllowDefaultParams(kFALSE),
  fPassNumber(-1),
  fOADBParam("muonTrackCutsParam")
{
  /// Constructor
  SetDefaultFilterMask();
}


//________________________________________________________________________
AliMuonTrackCuts::AliMuonTrackCuts(const AliMuonTrackCuts& obj) :
  AliAnalysisCuts(obj),
  fIsMC(obj.fIsMC),
  fUseCustomParam(obj.fUseCustomParam),
  fSharpPtCut(obj.fSharpPtCut),
  fAllowDefaultParams(obj.fAllowDefaultParams),
  fPassNumber(obj.fPassNumber),
  fOADBParam(obj.fOADBParam)
{
  /// Copy constructor
}


//________________________________________________________________________
AliMuonTrackCuts& AliMuonTrackCuts::operator=(const AliMuonTrackCuts& obj)
{
  /// Assignment operator
  if ( this != &obj ) { 
    AliAnalysisCuts::operator=(obj);
    fIsMC = obj.fIsMC;
    fUseCustomParam = obj.fUseCustomParam;
    fSharpPtCut = obj.fSharpPtCut;
    fAllowDefaultParams = obj.fAllowDefaultParams;
    fPassNumber = obj.fPassNumber;
    fOADBParam = obj.fOADBParam;
  }
  return *this;
}


//________________________________________________________________________
AliMuonTrackCuts::~AliMuonTrackCuts()
{
  /// Destructor
}

//________________________________________________________________________
void AliMuonTrackCuts::SetCustomParamFromRun( Int_t runNumber, Int_t passNumber )
{
  /// It first searches the default parameters in OADB
  /// then disables the access to the OADB
  /// and allows to manually modify parameters

  fPassNumber = passNumber;
  ReadParamFromOADB ( runNumber, passNumber );
  fUseCustomParam = kTRUE;
  //AliWarning (Form("Setting parameters from run %i pass %i. From now on SetRun does NOTHING!!", runNumber, passNumber));
  AliWarning (Form("From now on SetRun does NOTHING!!"));
}


//________________________________________________________________________
AliOADBMuonTrackCutsParam* AliMuonTrackCuts::CustomParam ( )
{
  /// Returning the muon track cuts parameters (not const, so you can change the parameters)
  /// CAVEAT: if you only want to Get the parameters, please use GetMuonTrackCutsParam()
  /// If you want to modify the parameters, you need to call SetCustomParamFromRun at least once,
  /// otherwise CustomParam returns a null pointer.
  
  if ( ! fUseCustomParam ) {
    AliError("This method allows you to modify the paramaters.\nIf you only want to get them, please use GetMuonTrackCutsParam instead.\nOtherwise, please call at least once SetCustomParamFromRun.");
    return 0x0;
  }
  return &fOADBParam;
}

//________________________________________________________________________
Bool_t AliMuonTrackCuts::SetRun ( const AliInputEventHandler* eventHandler )
{
  /// Get parameters from OADB for current run
  
  if ( fUseCustomParam ) return kFALSE;
  Int_t runNumber = eventHandler->GetEvent()->GetRunNumber();
  
  Int_t passNumber = fPassNumber;
  if ( fPassNumber < 0 && ! fAllowDefaultParams ) {
    // Pass number not set by user: try to guess it from data
    passNumber = AliAnalysisMuonUtility::GetPassNumber(eventHandler);
    AliInfo(Form("Guessing pass number from path: pass%i", passNumber));
  }
  return ReadParamFromOADB ( runNumber, passNumber );
}


//________________________________________________________________________
Bool_t AliMuonTrackCuts::ReadParamFromOADB ( Int_t runNumber, Int_t passNumber )
{

  /// Read parameters from OADB
  
  if ( passNumber < 0 && ! fAllowDefaultParams ) AliFatal("Pass number not specified!");
  
  TString filename = Form("%s/PWG/MUON/MuonTrackCuts.root",AliAnalysisManager::GetOADBPath());
  if ( fIsMC ) filename.ReplaceAll(".root", "_MC.root");

  TFile* file = TFile::Open(filename.Data(), "READ");
  if ( ! file ) {
    AliFatal(Form("OADB file %s not found!", filename.Data()));
    return kFALSE;
  }

  // Search the container name to find the correct pass
  AliOADBContainer* oadbContainer = 0x0;
  AliOADBMuonTrackCutsParam* runMatchParams = 0x0, *lastMatchParams = 0x0, *lastDefaultParams = 0x0;
  
  Int_t lastDefaultPass = -999, lastMatchPass = -999, foundPass = -999;
  TList* listOfKeys = file->GetListOfKeys();
  TIter next(listOfKeys);
  TObject* key = 0x0;
  // loop on keys
  while ( ( key = next() ) ) {
    Int_t currPass = AliAnalysisMuonUtility::GetPassNumber(key->GetName());
    // if user selects a specific pass number, check for it
    if ( fPassNumber >= 0 && currPass != fPassNumber ) continue;
    
    oadbContainer = static_cast<AliOADBContainer*> (file->Get(key->GetName()));    
    // Check if the found parameters are default or match the requested run
    AliOADBMuonTrackCutsParam* currParams = static_cast<AliOADBMuonTrackCutsParam*> (oadbContainer->GetObject(runNumber, "default"));
    if ( oadbContainer->GetDefaultObject(currParams->GetName()) ) {
      if ( currPass > lastDefaultPass ) {
        lastDefaultPass = currPass;
        lastDefaultParams = currParams;
      }
    }
    else {
      if ( currPass == passNumber ) {
        foundPass = currPass;
        runMatchParams = currParams;
        break;
      }
      else if ( currPass > lastMatchPass ) {
        lastMatchPass = currPass;
        lastMatchParams = currParams;
      }
    }
  } // loop on keys
  
  if ( ! oadbContainer ) {
    file->Close();
    AliFatal(Form("Requested pass%i not found!", passNumber));
    return kFALSE; // Not needed, but Coverity could complain
  }
  
  if ( runMatchParams ) fOADBParam = *runMatchParams;
  else if ( fAllowDefaultParams ) {
    AliOADBMuonTrackCutsParam* currParams = ( lastMatchParams ) ? lastMatchParams : lastDefaultParams;
    if ( ! currParams ) { // This case should be impossible, but Coverity complains
      AliFatal("No parameter found");
      return kFALSE;
    }
    foundPass = ( lastMatchParams ) ? lastMatchPass : lastDefaultPass;
    fOADBParam = *currParams;
    AliWarning(Form("Requested run %i not found in pass%i: using %s (pass%i)", runNumber, passNumber, fOADBParam.GetName(),foundPass));
  }
  else AliFatal(Form("Requested run %i not found in pass%i!", runNumber, passNumber));
  
  file->Close();

  AliInfo(Form("Requested run %i pass%i. Param. set: %s (pass%i)", runNumber, passNumber, fOADBParam.GetName(), foundPass));
  
  return kTRUE;
}

//________________________________________________________________________
Bool_t AliMuonTrackCuts::IsSelected( TObject* obj )
{
  /// Track is selected
  UInt_t filterMask = GetFilterMask();
  UInt_t selectionMask = GetSelectionMask(obj);
  
  AliDebug(1, Form("IsMuon %i  selected %i  mask 0x%x", AliAnalysisMuonUtility::IsMuonTrack(static_cast<AliVParticle*>(obj)), ( selectionMask & filterMask ) == filterMask, selectionMask ));
  
  return ( ( selectionMask & filterMask ) == filterMask );
}


//________________________________________________________________________
UInt_t AliMuonTrackCuts::GetSelectionMask( const TObject* obj )
{
  /// Get selection mask
  
  const AliVParticle* track = static_cast<const AliVParticle*> ( obj );
  
  UInt_t selectionMask = 0;

  if ( ! AliAnalysisMuonUtility::IsMuonTrack(track) ) return selectionMask;

  Double_t eta = track->Eta();
  if ( eta > -4. && eta < -2.5 ) selectionMask |= kMuEta;

  Double_t thetaAbsEndDeg = AliAnalysisMuonUtility::GetThetaAbsDeg(track);

  if ( thetaAbsEndDeg > 2. && thetaAbsEndDeg < 10. ) selectionMask |= kMuThetaAbs;

  Int_t matchTrig = AliAnalysisMuonUtility::GetMatchTrigger(track);
  Int_t cutLevel[3] = {kMuMatchApt, kMuMatchLpt, kMuMatchHpt};
  Double_t pt = track->Pt();
  for ( Int_t ilevel=0; ilevel<3; ilevel++ ) {
    if ( matchTrig < ilevel+1 ) break;
    if ( fSharpPtCut && pt < fOADBParam.GetSharpPtCut(ilevel) ) break;
    selectionMask |= cutLevel[ilevel];
  }

  if ( AliAnalysisMuonUtility::GetChi2perNDFtracker(track) < fOADBParam.GetChi2NormCut() ) selectionMask |= kMuTrackChiSquare;

  TVector3 dcaAtVz = GetCorrectedDCA(track);
  Double_t pTotMean = GetAverageMomentum(track);

  Double_t pDca = pTotMean * dcaAtVz.Mag();
    
  Double_t pTot = track->P();
  
  Double_t sigmaPdca = IsThetaAbs23(track) ? fOADBParam.GetSigmaPdca23() : fOADBParam.GetSigmaPdca310();
  
  // Momentum resolution only
  // The cut depends on the momentum resolution. In particular:
  // Sigma_pDCA = Sqrt( Sigma_pDCA_measured^2 + Sigma_p/p * pDCA)
  // The relative momentum distribution Sigma_p/p is estimated from the track Delta_p,
  // and from the error on sagitta Delta_s:
  // Delta_p/p = p*Delta_s/(1+p*Delta_s)
  // A cut at N sigmas requres a cut in N Delta_s, so
  // Sigma_p/p(N) = p*N*Delta_s/(1+p*N*Delta_s)
  //Double_t pResolutionEffect = pDca * pTot * GetRelPResolution() / ( 1. + GetNSigmaPdca() * GetRelPResolution()*pTot );
  
  //Double_t pResolutionEffect = 0.4 * pTot;  // Values used in 2010 data
  //Double_t pResolutionEffect = 0.32 * pTot; // Values in 2011
  //Double_t sigmaPdcaWithRes = TMath::Sqrt( sigmaPdca*sigmaPdca + pResolutionEffect*pResolutionEffect );
  
  // Momentum resolution and slope resolution 
  // Due to the momentum resolution, the measured momentum is biased
  // Since we want to keep as much signal as possible, we want to avoid
  // that a measured pxDCA is rejected since the momentum is overestimated
  // p_true = p_meas - Delta_p
  // p_true = p_meas - N*Delta_s*p_meas / (1+n*Delta_s*p_meas)
  // Hence:
  // p_true x DCA < N * Sigma_pDCA_meas =>
  // p_meas x DCA < N * Sigma_pDCA_meas / ( 1 - N*Delta_s*p_meas / (1+n*Delta_s*p_meas))
  // Finally the cut value has to be summed in quadrature with the error on DCA,
  // which is given by the slope resolution
  // p_meas x DCA < N * Sqrt( ( Sigma_pDCA_meas / ( 1 - N*Delta_s*p_meas / (1+n*Delta_s*p_meas)) )^2 + (distance * sigma_slope * p_meas )^2)
  Double_t nrp = fOADBParam.GetNSigmaPdca() * fOADBParam.GetRelPResolution() * pTot;
  Double_t pResolutionEffect = sigmaPdca / ( 1. - nrp / ( 1. + nrp ) );
  Double_t slopeResolutionEffect = 535. * fOADBParam.GetSlopeResolution() * pTot;
  
  Double_t sigmaPdcaWithRes = TMath::Sqrt( pResolutionEffect*pResolutionEffect + slopeResolutionEffect*slopeResolutionEffect );
  
  if ( pDca < fOADBParam.GetNSigmaPdca() * sigmaPdcaWithRes ) selectionMask |= kMuPdca;
  
  AliDebug(1, Form("Selection mask 0x%x\n", selectionMask));

  return selectionMask;
}


//________________________________________________________________________
Bool_t AliMuonTrackCuts::IsSelected( TList* /* list */)
{
  /// Not implemented
  AliError("Function not implemented: Use IsSelected(TObject*)");
  return kFALSE;
}


//________________________________________________________________________
Bool_t AliMuonTrackCuts::IsThetaAbs23 ( const AliVParticle* track ) const
{
  /// Check if theta_abs is smaller than 3 degrees
  return ( AliAnalysisMuonUtility::GetThetaAbsDeg(track) < 3. );
}


//________________________________________________________________________
TVector3 AliMuonTrackCuts::GetCorrectedDCA ( const AliVParticle* track ) const
{
  /// Get corrected DCA
  
  TVector3 vertex(AliAnalysisMuonUtility::GetXatVertex(track), AliAnalysisMuonUtility::GetYatVertex(track), AliAnalysisMuonUtility::GetZatVertex(track));
  
  TVector3 dcaTrack(AliAnalysisMuonUtility::GetXatDCA(track), AliAnalysisMuonUtility::GetYatDCA(track), AliAnalysisMuonUtility::GetZatDCA(track));
  
  TVector3 dcaAtVz = dcaTrack - vertex - fOADBParam.GetMeanDCA();

  return dcaAtVz;
}

//________________________________________________________________________
Double_t AliMuonTrackCuts::GetAverageMomentum ( const AliVParticle* track ) const
{
  /// Get average momentum before and after the absorber
  
  Double_t pTotMean = 0.;
  Double_t pTot = track->P();
  //if ( isESD ) pTotMean = 0.5 * ( pTot + ((AliESDMuonTrack*)track)->PUncorrected() );
  if ( ! AliAnalysisMuonUtility::IsAODTrack(track) ) pTotMean = ((AliESDMuonTrack*)track)->PUncorrected(); // Increased stability if using uncorrected value
  else {
    Double_t meanPcorr = IsThetaAbs23(track) ? fOADBParam.GetMeanPCorr23() : fOADBParam.GetMeanPCorr310();
    pTotMean = pTot - meanPcorr;
  }

  return pTotMean;
}


//________________________________________________________________________
void AliMuonTrackCuts::SetDefaultFilterMask ()
{
  /// Standard cuts for single muon
  SetFilterMask ( kMuEta | kMuThetaAbs | kMuPdca | kMuMatchApt );
}

//________________________________________________________________________
Bool_t AliMuonTrackCuts::TrackPtCutMatchTrigClass ( const AliVParticle* track, const TArrayI ptCutFromClass) const
{
  /// Check if track passes the trigger pt cut level used in the trigger class
  Int_t matchTrig = AliAnalysisMuonUtility::GetMatchTrigger(track);
  Bool_t matchTrackerPt = kTRUE;
  if ( IsApplySharpPtCutInMatching() ) {
    matchTrackerPt = ( track->Pt() >= fOADBParam.GetSharpPtCut(ptCutFromClass[0]-1,kFALSE) );
  }
  Bool_t passCut = ( ( matchTrig >= ptCutFromClass[0] ) && matchTrackerPt );
  AliDebug(1,Form("Class matchTrig %i %i  trackMatchTrig %i  trackPt %g (required %i)  passCut %i", ptCutFromClass[0], ptCutFromClass[1], matchTrig, track->Pt(), IsApplySharpPtCutInMatching(),passCut));
  return passCut;
}


//________________________________________________________________________
void AliMuonTrackCuts::Print(Option_t* option) const
{
  //
  /// Print info
  //
  TString sopt(option);
  sopt.ToLower();
  if ( sopt.IsNull() || sopt.Contains("*") || sopt.Contains("all") ) sopt = "mask param";
  UInt_t filterMask = GetFilterMask();
  Int_t cutLevel[3] = {kMuMatchApt, kMuMatchLpt, kMuMatchHpt};
  TString cutLevelName[3] = {"Apt", "Lpt", "Hpt"};
  if ( sopt.Contains("mask") ) {
    printf(" *** Muon track filter mask: *** \n");
    printf("  0x%x\n", filterMask);
    if ( filterMask & kMuEta ) printf("  -4 < eta < -2.5\n");
    if ( filterMask & kMuThetaAbs ) printf("  2 < theta_abs < 10 deg\n");
    if ( filterMask & kMuPdca ) printf("  pxDCA cut\n");
    for ( Int_t ilevel=0; ilevel<3; ilevel++ ) {
      if ( filterMask & cutLevel[ilevel] ) {
        printf("  match %s", cutLevelName[ilevel].Data());
        if ( fSharpPtCut ) printf(" && sharp pt from tracker");
        printf("\n");
      }
    }
    if ( filterMask & kMuTrackChiSquare ) printf("  Chi2 cut on track\n");
    printf(" ******************** \n");
  }
  if ( sopt.Contains("param") ) fOADBParam.Print();
}
