// --------------------------------------------------------------------------
//                   OpenMS -- Open-Source Mass Spectrometry
// --------------------------------------------------------------------------
// Copyright The OpenMS Team -- Eberhard Karls University Tuebingen,
// ETH Zurich, and Freie Universitaet Berlin 2002-2017.
//
// This software is released under a three-clause BSD license:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of any author or any participating institution
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
// For a full list of authors, refer to the file AUTHORS.
// --------------------------------------------------------------------------
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL ANY OF THE AUTHORS OR THE CONTRIBUTING
// INSTITUTIONS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// --------------------------------------------------------------------------
// $Maintainer: Julianus Pfeuffer $
// $Authors: Julianus Pfeuffer $
// --------------------------------------------------------------------------

#include <OpenMS/ANALYSIS/ID/IDMergerAlgorithm.h>
#include <OpenMS/METADATA/PeptideIdentification.h>
#include <unordered_map>

using namespace std;
namespace OpenMS
{
  IDMergerAlgorithm::IDMergerAlgorithm(const String& runIdentifier) :
      IDMergerAlgorithm::DefaultParamHandler("IDMergerAlgorithm"),
      protResult(),
      pepResult(),
      id(runIdentifier)
  {
    defaults_.setValue("annotate_origin",
                       "true",
                       "If true, adds a map_index MetaValue to the PeptideIDs to annotate the IDRun they came from.");
    defaults_.setValidStrings("annotate_origin", ListUtils::create<String>("true,false"));
    defaultsToParam_();
    protResult.setIdentifier(getNewIdentifier_());
  }


  void IDMergerAlgorithm::insertRun(
      std::vector<ProteinIdentification>& prots,
      std::vector<PeptideIdentification>& peps
      )
  {
    if (prots.empty())
    {
      //TODO actually an error
      return;
    }
    if (peps.empty())
    {
      //TODO return warning, nothing is inserted
      return;
    }

    if (!filled)
    {
      if (prots.size() > 1)
      {
        //Without any exp. design we assume label-free for checking mods
        checkOldRunConsistency_(prots, "label-free");
      }
      //TODO merge SearchParams e.g. in case of SILAC mods
      copySearchParams_(prots[0], protResult);
      filled = true;
    }
    else
    {
      //Without any exp. design we assume label-free for checking mods
      checkOldRunConsistency_(prots, this->protResult, "label-free");
    }

    movePepIDsAndRefProteinsToResult_(peps, prots);
  }

  void IDMergerAlgorithm::returnResultsAndClear(
      ProteinIdentification& prots,
      vector<PeptideIdentification>& peps)
  {
    // convert the map from file origin to idx into
    // a vector
    StringList newOrigins(fileOriginToIdx.size());
    for (auto& entry : fileOriginToIdx)
    {
      newOrigins[entry.second] = std::move(entry.first);
    }
    // currently setPrimaryMSRunPath does not support move (const ref)
    protResult.setPrimaryMSRunPath(std::move(newOrigins));
    std::swap(prots, protResult);
    std::swap(peps, pepResult);
    //reset so the new result is usable right away
    protResult = ProteinIdentification{};
    protResult.setIdentifier(getNewIdentifier_());
    //clear, if user gave non-empty vector
    pepResult.clear();
    //reset internals
    fileOriginToIdx.clear();
    proteinsCollected.clear();
  }

  String IDMergerAlgorithm::getNewIdentifier_() const
  {
    std::array<char, 64> buffer;
    buffer.fill(0);
    time_t rawtime;
    time(&rawtime);
    const auto timeinfo = localtime(&rawtime);
    strftime(buffer.data(), sizeof(buffer), "%d-%m-%Y %H-%M-%S", timeinfo);
    return id + String(buffer.data());
  }


  void IDMergerAlgorithm::movePepIDsAndRefProteinsToResult_(
      vector<PeptideIdentification>& pepIDs,
      vector<ProteinIdentification>& oldProtRuns
  )
  {
    bool annotate_origin(param_.getValue("annotate_origin").toBool());
    vector<StringList> originFiles{};
    for (const auto& protRun : oldProtRuns)
    {
      StringList toFill{};
      protRun.getPrimaryMSRunPath(toFill);
      if (toFill.empty() && annotate_origin)
      {
        throw Exception::MissingInformation(
            __FILE__,
            __LINE__,
            OPENMS_PRETTY_FUNCTION,
            "Annotation of origin requested during merge, but no origin present in run "
            + protRun.getIdentifier() + ".");
      }
      originFiles.push_back(toFill);
      for (String& f : toFill)
      {
        fileOriginToIdx.emplace(std::move(f), fileOriginToIdx.size());
      }
      toFill.clear();
    }

    for (auto &pid : pepIDs)
    {
      const String &runID = pid.getIdentifier();

      //TODO maybe create lookup table in the beginning
      Size oldProtRunIdx = 0;
      for (; oldProtRunIdx < oldProtRuns.size(); ++oldProtRunIdx)
      {
        ProteinIdentification &protIDRun = oldProtRuns[oldProtRunIdx];
        if (protIDRun.getIdentifier() == runID)
        {
          break;
        }
      }
      if (oldProtRunIdx == oldProtRuns.size())
      {
        throw Exception::MissingInformation(
            __FILE__,
            __LINE__,
            OPENMS_PRETTY_FUNCTION,
            "Old IdentificationRun not found for PeptideIdentification "
            "(" + String(pid.getMZ()) + ", " + String(pid.getRT()) + ").");

      }

      bool annotated = pid.metaValueExists("map_index");
      if (annotate_origin || annotated)
      {
        Size oldFileIdx(0);
        if (annotated)
        {
          oldFileIdx = pid.getMetaValue("map_index");
        }
          // If there is more than one possible file it might be from
          // and it is not annotated -> fail
        else if (originFiles[oldProtRunIdx].size() > 1)
        {
          throw Exception::MissingInformation(
              __FILE__,
              __LINE__,
              OPENMS_PRETTY_FUNCTION,
              "Trying to annotate new map_index for PeptideIdentification "
              "(" + String(pid.getMZ()) + ", " + String(pid.getRT()) + ") but"
                                                                       "no old map_index present");
        }
        pid.setMetaValue("map_index", fileOriginToIdx[originFiles[oldProtRunIdx].at(oldFileIdx)]);
      }
      pid.setIdentifier(protResult.getIdentifier());
      for (auto &phit : pid.getHits())
      {
        //TODO think about getting the set first and then look for each acc
        for (auto &acc : phit.extractProteinAccessionsSet())
        {
          const auto &it = proteinsCollected.emplace(acc);
          if (it.second) // was newly inserted
          {
            protResult.getHits().emplace_back(std::move(*oldProtRuns[oldProtRunIdx].findHit(acc)));
          }
        }
      }
      //move peptides into right vector
      pepResult.emplace_back(std::move(pid));
    }
  }

  void IDMergerAlgorithm::copySearchParams_(ProteinIdentification& from, ProteinIdentification& to)
  {
      to.setSearchEngine(from.getSearchEngine());
      to.setSearchEngineVersion(from.getSearchEngineVersion());
      to.setSearchParameters(from.getSearchParameters());
  }

  bool IDMergerAlgorithm::checkOldRunConsistency_(const vector<ProteinIdentification>& protRuns, const String& experiment_type) const
  {
    return checkOldRunConsistency_(protRuns, protRuns[0], experiment_type);
  }

  bool IDMergerAlgorithm::checkOldRunConsistency_(const vector<ProteinIdentification>& protRuns, const ProteinIdentification& ref, const String& experiment_type) const
  {
    const String& engine = ref.getSearchEngine();
    const String& version = ref.getSearchEngineVersion();
    ProteinIdentification::SearchParameters params = ref.getSearchParameters();
    set<String> fixed_mods(params.fixed_modifications.begin(), params.fixed_modifications.end());
    set<String> var_mods(params.variable_modifications.begin(), params.variable_modifications.end());
    bool ok = false;
    unsigned runID = 0;
    for (const auto& idRun : protRuns)
    {
      ok = true;
      if (idRun.getSearchEngine() != engine || idRun.getSearchEngineVersion() != version)
      {
        ok = false;
        LOG_WARN << "Search engine " + idRun.getSearchEngine() + "from IDRun " + String(runID) + " does not match "
                                                                                                 "with the others. You probably do not want to merge the results with this tool.";
        break;
      }
      const ProteinIdentification::SearchParameters& sp = idRun.getSearchParameters();
      if (params.precursor_mass_tolerance != sp.precursor_mass_tolerance ||
          params.precursor_mass_tolerance_ppm != sp.precursor_mass_tolerance_ppm ||
          params.db != sp.db ||
          params.db_version != sp.db_version ||
          params.fragment_mass_tolerance != sp.fragment_mass_tolerance ||
          params.fragment_mass_tolerance_ppm != sp.fragment_mass_tolerance_ppm ||
          params.charges != sp.charges ||
          params.digestion_enzyme != sp.digestion_enzyme ||
          params.taxonomy != sp.taxonomy)
      {
        ok = false;
        LOG_WARN << "Searchengine settings from IDRun " + String(runID) + " does not match with the others."
                                                                          " You probably do not want to merge the results with this tool if they differ significantly.";
        break;
      }

      set<String> curr_fixed_mods(sp.fixed_modifications.begin(), sp.fixed_modifications.end());
      set<String> curr_var_mods(sp.variable_modifications.begin(), sp.variable_modifications.end());
      if (fixed_mods != curr_fixed_mods ||
          var_mods != curr_var_mods)
      {
        if (experiment_type != "labeled_MS1")
        {
          ok = false;
          LOG_WARN << "Used modification settings from IDRun " + String(runID) + " does not match with the others."
                                                                                 " Since the experiment is not annotated as MS1-labeled you probably do not want to merge the results with this tool.";
          break;
        }
        else
        {
          //TODO actually introduce a flag for labelling modifications in the Mod datastructures?
          //OR put a unique ID for the used mod as a UserParam to the mapList entries (consensusHeaders)
          //TODO actually you would probably need an experimental design here, because
          //settings have to agree exactly in a FractionGroup but can slightly differ across runs.
          LOG_WARN << "Used modification settings from IDRun " + String(runID) + " does not match with the others."
                                                                                 " Although it seems to be an MS1-labeled experiment, check carefully that only non-labelling mods differ.";
        }
      }
    }
    if (!ok /*&& TODO and no force flag*/)
    {
      throw Exception::BaseException(__FILE__,
                                     __LINE__,
                                     OPENMS_PRETTY_FUNCTION,
                                     "InvalidData",
                                     "Search settings are not matching across IdentificationRuns. "
                                     "See warnings. Aborting..");
    }
    return ok;
  }
} // namespace OpenMS