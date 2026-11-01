#ifndef VITA_LIVEAREA_H
#define VITA_LIVEAREA_H

class StageStats;

/**
 * @brief Record the just-finished stage into the recent-songs history and push
 * the list to the "Recent songs" text frame on the PS Vita LiveArea.
 *
 * Only compiled on Vita (src/arch/Vita/VitaLiveArea.cpp); call sites must be
 * guarded by #ifdef VITA.
 */
void VitaLiveAreaRecordStage( const StageStats *pStageStats );

#endif
