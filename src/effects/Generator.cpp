/**********************************************************************

  Audacity: A Digital Audio Editor

  Generator.h

  Two Abstract classes, Generator, and BlockGenerator, that effects which
  generate audio should derive from.

  Block Generator breaks the synthesis task up into smaller parts.

  Dominic Mazzoni
  Vaughan Johnson

**********************************************************************/

#include "Generator.h"
#include "EffectOutputTracks.h"

#include "Project.h"
#include "Prefs.h"
#include "SyncLock.h"
#include "ViewInfo.h"
#include "WaveTrack.h"

#include "TimeWarper.h"

#include "AudacityMessageBox.h"

bool Generator::Process(EffectInstance &, EffectSettings &settings)
{
   const auto duration = settings.extra.GetDuration();

   // Set up mOutputTracks.
   // This effect needs all for sync-lock grouping.
   EffectOutputTracks outputs{ *mTracks, true };

   // Iterate over the tracks
   bool bGoodResult = true;
   int ntrack = 0;

   outputs.Get().Any().VisitWhile(bGoodResult,
      [&](auto &&fallthrough){ return [&](WaveTrack &track) {
         if (!track.GetSelected())
            return fallthrough();
         bool editClipCanMove = GetEditClipsCanMove();

         //if we can't move clips, and we're generating into an empty space,
         //make sure there's room.
         if (!editClipCanMove &&
             track.IsEmpty(mT0, mT1 + 1.0 / track.GetRate()) &&
             !track.IsEmpty(mT0,
               mT0 + duration - (mT1 - mT0) - 1.0 / track.GetRate()))
         {
            EffectUIServices::DoMessageBox(*this,
               XO("There is not enough room available to generate the audio"),
               wxICON_STOP,
               XO("Error") );
            Failure();
            bGoodResult = false;
            return;
         }

         if (duration > 0.0)
         {
            auto pProject = FindProject();
            // Create a temporary track
            auto tmp = track.EmptyCopy();
            BeforeTrack(track);
            BeforeGenerate();

            // Fill it with data
            if (!GenerateTrack(settings, &*tmp, track, ntrack))
               bGoodResult = false;
            else {
               // Transfer the data from the temporary track to the actual one
               tmp->Flush();
               PasteTimeWarper warper{ mT1, mT0 + duration };
               const auto &selectedRegion =
                  ViewInfo::Get( *pProject ).selectedRegion;
               track.ClearAndPaste(
                  selectedRegion.t0(), selectedRegion.t1(),
                  &*tmp, true, false, &warper);
            }

            if (!bGoodResult) {
               Failure();
               return;
            }
         }
         else
         {
            // If the duration is zero, there's no need to actually
            // generate anything
            track.Clear(mT0, mT1);
         }

         ntrack++;
      }; },
      [&](Track &t) {
         if (SyncLock::IsSyncLockSelected(&t)) {
            t.SyncLockAdjust(mT1, mT0 + duration);
         }
      }
   );

   if (bGoodResult) {
      Success();

      if (bGoodResult)
         outputs.Commit();

      mT1 = mT0 + duration; // Update selection.
   }

   return bGoodResult;
}

bool BlockGenerator::GenerateTrack(EffectSettings &settings,
   WaveTrack *tmp, const WaveTrack &track, int ntrack)
{
   bool bGoodResult = true;
   numSamples = track.TimeToLongSamples(settings.extra.GetDuration());
   decltype(numSamples) i = 0;
   Floats data{ tmp->GetMaxBlockSize() };

   while ((i < numSamples) && bGoodResult) {
      const auto block =
         limitSampleBufferSize( tmp->GetBestBlockSize(i), numSamples - i );

      GenerateBlock(data.get(), track, block);

      // Add the generated data to the temporary track
      tmp->Append((samplePtr)data.get(), floatSample, block);
      i += block;

      // Update the progress meter
      if (TrackProgress(ntrack,
                        i.as_double() /
                        numSamples.as_double()))
         bGoodResult = false;
   }
   return bGoodResult;
}
