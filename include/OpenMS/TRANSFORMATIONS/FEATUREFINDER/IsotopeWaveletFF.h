// -*- Mode: C++; tab-width: 2; -*-
// vi: set ts=2:
//
// --------------------------------------------------------------------------
//                   OpenMS Mass Spectrometry Framework
// --------------------------------------------------------------------------
//  Copyright (C) 2003-2007 -- Oliver Kohlbacher, Knut Reinert
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2.1 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// --------------------------------------------------------------------------
// $Maintainer: Rene Hussong$
// --------------------------------------------------------------------------

#ifndef OPENMS_TRANSFORMATIONS_FEATUREFINDER_ISOTOPEWAVELET_FF_H
#define OPENMS_TRANSFORMATIONS_FEATUREFINDER_ISOTOPEWAVELET_FF_H

#ifndef NULL
#define NULL 0
#endif

#include <OpenMS/TRANSFORMATIONS/FEATUREFINDER/IsotopeWaveletTransform.h>
#include <OpenMS/TRANSFORMATIONS/FEATUREFINDER/FeatureFinderAlgorithm.h>
#include <OpenMS/CONCEPT/ProgressLogger.h>
#include <OpenMS/KERNEL/FeatureMap.h>
#include <iostream>


namespace OpenMS
{
	/** @brief Implements the isotope wavelet feature finder.
	 *
	 * 	The IsotopeWaveletFF class has been designed for finding features in 1D or 2D MS data sets using the isotope wavelet.
	 * 	In the case of two dimensional data, the class provides additionally the sweep line algorithm. Please not that in
	 * 	its current implementation the istope wavelet feature finder is only applicable to raw data (not to picked data). 
	 *
	 * 	Before you start the algorithm by calling runFF, you have to set up the class by a call to initializeFF. 
	 * 	Please note that this class features a singleton implementation, i.e. yo cannot directly intantiate this
	 * 	class by a call to its default constructor.
	 *
	 *  @ref IsotopeWaveletFF_Parameters are explained on a separate page.
	 *	@ingroup FeatureFinder */
	template <typename PeakType, typename FeatureType>
	class IsotopeWaveletFF : public FeatureFinderAlgorithm<PeakType, FeatureType> 
	{

		public:

			typedef FeatureFinderAlgorithm<PeakType, FeatureType> Base;

			/** @brief Default Constructor */
			IsotopeWaveletFF() throw()
			{ 
				Base::defaults_.setValue ("max_charge", 1, "The maximal charge state to be considered.", false);
				Base::defaults_.setValue ("intensity_threshold", 0.1, "The final threshold t' is build upon the formula: t' = av+t*sd\n" 
														"where t is the intensity_threshold, av the average intensity within the wavelet transformed signal\n" 
														"and sd the standard deviation of the transform.\n"
														"If you set intensity_threshold=-1, t' will be zero.\n"
														"For single scan analysis (e.g. MALDI peptide fingerprints) you should start with an intensity_threshold\n"
														"around 0..1 and increase it if necessary.", false);
				Base::defaults_.setValue ("rt_votes_cutoff", 5, "A parameter of the sweep line algorithm. It determines the minimum number of\n"
														"subsequent scans a pattern must occur to be considered as a feature.", false);
				Base::defaults_.setValue ("rt_interleave", 2, "A parameter of the sweep line algorithm. It determines the maximum number of\n"
														"scans (w.r.t. rt_votes_cutoff) where a pattern is missing.", true);
				Base::defaults_.setValue ("recording_mode", 1, "Determines if the spectra have been recorded in positive ion (1) or\n" 
																	"negative ion (-1) mode.", true);
				Base::defaults_.setValue ("create_Mascot_PMF_File", 0, "Creates a peptide mass fingerprint file for a direct query of MASCOT.\n" 
																	"In the case the data file contains several spectra, an additional column indication the elution time\n"
																	"will be included.", true);
				Base::defaultsToParam_();
			}


			/** @brief Destructor. */		
			virtual ~IsotopeWaveletFF() throw ()
			{
			}	


			/** @brief The working horse of this class. */
			void run ()
			{
				DoubleReal max_mz = Base::map_->getMax()[1];
				IsotopeWavelet::setMaxCharge(max_charge_);
				IsotopeWavelet::computeIsotopeDistributionSize(max_mz);
				IsotopeWavelet::preComputeExpensiveFunctions(max_mz);
				
				IsotopeWaveletTransform<PeakType> iwt (max_charge_, create_Mascot_PMF_File_);
		
				Base::ff_->setLogType (ProgressLogger::CMD);
				Base::ff_->startProgress (0, 3*Base::map_->size(), "analyzing spectra");  

				UInt RT_votes_cutoff = RT_votes_cutoff_;
				//Check for useless RT_votes_cutoff_ parameter
				if (RT_votes_cutoff_ > Base::map_->size())
					RT_votes_cutoff = 0;
				

				for (UInt i=0, j=0; i<Base::map_->size(); ++i)
				{	
					std::vector<MSSpectrum<PeakType> > pwts (max_charge_, Base::map_->at(i));
					#ifdef OPENMS_DEBUG
						std::cout << "Spectrum " << i << " (" << Base::map_->at(i).getRT() << ") of " << Base::map_->size()-1 << " ... " ; 
						std::cout.flush();
					#endif
					
					IsotopeWaveletTransform<PeakType>::getTransforms (Base::map_->at(i), pwts, max_charge_, mode_);
					Base::ff_->setProgress (++j);

					#ifdef OPENMS_DEBUG
						std::cout << "transform ok ... "; std::cout.flush();
					#endif
					
					iwt.identifyCharges (pwts,  Base::map_->at(i), i, ampl_cutoff_);
					Base::ff_->setProgress (++j);

					#ifdef OPENMS_DEBUG
						std::cout << "charge recognition ok ... "; std::cout.flush();
					#endif

					iwt.updateBoxStates(i, RT_interleave_, RT_votes_cutoff);
					Base::ff_->setProgress (++j);

					#ifdef OPENMS_DEBUG
						std::cout << "updated box states." << std::endl;
					#endif

					std::cout.flush();
				};

				Base::ff_->endProgress();
				
				//And now ... a cute hack ;-) 
				//Forces to empty OpenBoxes_ and to synchronize ClosedBoxes_ 
				iwt.updateBoxStates(INT_MAX, RT_interleave_, RT_votes_cutoff); 

				#ifdef OPENMS_DEBUG
					std::cout << "Final mapping."; std::cout.flush();
				#endif
	
				*Base::features_ = iwt.mapSeeds2Features (*Base::map_, max_charge_, RT_votes_cutoff_);
			}

			static const String getProductName()
			{ 
				return ("isotope_wavelet"); 
			}
					
			static FeatureFinderAlgorithm<PeakType,FeatureType>* create()
			{
				return new IsotopeWaveletFF();
			}


		protected:

			/** @brief Internally used data struture for the sweep line algorithm. */
			struct BoxElement
				{			
					DoubleReal mz;
					UInt c; ///<Note, this is not the charge (it is charge-1!!!)
					DoubleReal score;
					DoubleReal intens;
					DoubleReal RT; ///<The elution time (not the scan index)
				};				

			typedef std::map<UInt, BoxElement> Box; ///<Key: RT (index), value: BoxElement
			typedef DRawDataPoint<2> RawDataPoint2D; 

			UInt max_charge_; ///<The maximal charge state we will consider
			DoubleReal ampl_cutoff_; ///<The only parameter of the isotope wavelet
			UInt RT_votes_cutoff_; ///<The number of susequent scans a pattern must cover in order to be considered as signal 
			UInt RT_interleave_; ///<The numer of scans we allow to be missed within RT_votes_cutoff_
			Int mode_; ///<Negative or positive charged 
			Int create_Mascot_PMF_File_; ///<Determines wheter a ASCII file for peptide mass fingerprinting will be created

			void updateMembers_() throw()
			{
				max_charge_ = Base::param_.getValue ("max_charge"); 
				ampl_cutoff_ = Base::param_.getValue ("intensity_threshold");
				RT_votes_cutoff_ = Base::param_.getValue ("rt_votes_cutoff");
				RT_interleave_ = Base::param_.getValue ("rt_interleave");
				mode_ = Base::param_.getValue ("recording_mode");
				create_Mascot_PMF_File_ = Base::param_.getValue ("create_Mascot_PMF_File");
				IsotopeWavelet::setMaxCharge(max_charge_);
			}
	};

} //namespace

#endif 
