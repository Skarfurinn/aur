// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "chain.h"
#include "chainparams.h"
#include "primitives/block.h"
#include "uint256.h"
#include "util.h"

unsigned int static GravityWell(const CBlockIndex* pindexLast, const CBlockHeader *pblock, int64_t TargetBlocksSpacingSeconds, uint64_t PastBlocksMin, uint64_t PastBlocksMax) {

    const CBlockIndex  *BlockLastSolved             = pindexLast;
    const CBlockIndex  *BlockReading                = pindexLast;
    uint64_t             PastBlocksMass              = 0;
    int64_t             PastRateActualSeconds       = 0;
    int64_t             PastRateTargetSeconds       = 0;
    double              PastRateAdjustmentRatio     = double(1);
    uint256             PastDifficultyAverage;
    uint256             PastDifficultyAveragePrev;
    uint256             BlockReadingDifficulty;
    double              EventHorizonDeviation;
    double              EventHorizonDeviationFast;
    double              EventHorizonDeviationSlow;

if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 ||
        (uint64_t)BlockLastSolved->nHeight < PastBlocksMin) {
        return Params().ProofOfWorkLimit().GetCompact();
    }

    int64_t LatestBlockTime = BlockLastSolved->GetBlockTime();

    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (PastBlocksMax > 0 && i > PastBlocksMax) { break; }
        PastBlocksMass++;

        if (i == 1) {
            PastDifficultyAverage.SetCompact(BlockReading->nBits);
        } else {
            BlockReadingDifficulty.SetCompact(BlockReading->nBits);
            if (BlockReadingDifficulty > PastDifficultyAveragePrev) {
                PastDifficultyAverage = PastDifficultyAveragePrev + ((BlockReadingDifficulty - PastDifficultyAveragePrev) / i);
            } else {
                PastDifficultyAverage = PastDifficultyAveragePrev - ((PastDifficultyAveragePrev - BlockReadingDifficulty) / i);
            }
        }
        PastDifficultyAveragePrev = PastDifficultyAverage;

        PastRateActualSeconds = LatestBlockTime - BlockReading->GetBlockTime();
        PastRateTargetSeconds = TargetBlocksSpacingSeconds * PastBlocksMass;
        PastRateAdjustmentRatio = double(1);
        if (PastRateActualSeconds < 0) { PastRateActualSeconds = 0; }
        if (PastRateActualSeconds != 0 && PastRateTargetSeconds != 0) {
            PastRateAdjustmentRatio = double(PastRateTargetSeconds) / double(PastRateActualSeconds);
        }
        EventHorizonDeviation                   = 1 + (0.7084 * pow((double(PastBlocksMass)/double(144)), -1.228));
        EventHorizonDeviationFast               = EventHorizonDeviation;
        EventHorizonDeviationSlow               = 1 / EventHorizonDeviation;

        if (PastBlocksMass >= PastBlocksMin) {
            if ((PastRateAdjustmentRatio <= EventHorizonDeviationSlow) ||
                (PastRateAdjustmentRatio >= EventHorizonDeviationFast)) {
                assert(BlockReading);
                break;
            }
        }
        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }
        BlockReading = BlockReading->pprev;
    }

    uint256 bnNew(PastDifficultyAverage);
    if (PastRateActualSeconds != 0 && PastRateTargetSeconds != 0) {
        bnNew *= PastRateActualSeconds;
        bnNew /= PastRateTargetSeconds;
    }
    if (bnNew > Params().ProofOfWorkLimit()) { bnNew = Params().ProofOfWorkLimit(); }

    // debug print
    LogPrintf("GetNextWorkRequired RETARGET (KGW Original)\n");
    LogPrintf("PastRateAdjustmentRatio =  %g    PastRateTargetSeconds = %d    PastRateActualSeconds = %d\n",
               PastRateAdjustmentRatio, PastRateTargetSeconds, PastRateActualSeconds);
    LogPrintf("Before: %08x  %s\n", BlockLastSolved->nBits, uint256().SetCompact(BlockLastSolved->nBits).ToString());
    LogPrintf("After:  %08x  %s\n", bnNew.GetCompact(), bnNew.ToString());

    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock)
{
    unsigned int nProofOfWorkLimit = Params().ProofOfWorkLimit().GetCompact();

    // Genesis block
    if (pindexLast == NULL)
        return nProofOfWorkLimit;

    if (pindexLast->nHeight+1 < 135)
        return nProofOfWorkLimit;

    // Gravity well after block 5400
    if (pindexLast->nHeight+1 > 5400)
    {
        static const int64_t  BlocksTargetSpacing         = 5 * 60; // 5 minutes
        unsigned int          TimeDaySeconds              = 60 * 60 * 24;
        int64_t               PastSecondsMin              = TimeDaySeconds * 0.5;
        int64_t               PastSecondsMax              = TimeDaySeconds * 14;
        uint64_t               PastBlocksMin               = PastSecondsMin / BlocksTargetSpacing;
        uint64_t               PastBlocksMax               = PastSecondsMax / BlocksTargetSpacing;

        return GravityWell(pindexLast, pblock, BlocksTargetSpacing, PastBlocksMin, PastBlocksMax);
    }

    // Only change once per interval
    if ((pindexLast->nHeight+1) % Params().Interval() != 0)
    {
        if (Params().AllowMinDifficultyBlocks())
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + Params().TargetSpacing()*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % Params().Interval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }
    const CBlockIndex* pindexFirst = pindexLast;

    // Go back the full period unless it's the first retarget after genesis.
    if ((pindexLast->nHeight+1) != Params().Interval()-1)
    {
        for (int i = 0; pindexFirst && i < Params().Interval(); i++)
            pindexFirst = pindexFirst->pprev;
        assert(pindexFirst);
    }
    else
    {
        for (int i = 0; pindexFirst && i < Params().Interval()-1; i++)
            pindexFirst = pindexFirst->pprev;
        assert(pindexFirst);
    }

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - pindexFirst->GetBlockTime();
    LogPrintf("Block Time: %d \n", pindexLast->GetBlockTime());
    LogPrintf("  nActualTimespan = %d  before bounds\n", nActualTimespan);
    if (nActualTimespan < ((Params().TargetTimespan()*50)/75))
        nActualTimespan = ((Params().TargetTimespan()*50)/75);
    if (nActualTimespan > ((Params().TargetTimespan()*75)/50))
        nActualTimespan = ((Params().TargetTimespan()*75)/50);

    // Retarget
    uint256 bnNew;
    uint256 bnOld;
    bnNew.SetCompact(pindexLast->nBits);
    bnOld = bnNew;
    bnNew *= nActualTimespan;
    bnNew /= Params().TargetTimespan();

    if (bnNew > Params().ProofOfWorkLimit())
        bnNew = Params().ProofOfWorkLimit();

    /// debug print
    LogPrintf("GetNextWorkRequired RETARGET\n");
    LogPrintf("Params().TargetTimespan() = %d    nActualTimespan = %d\n", Params().TargetTimespan(), nActualTimespan);
    LogPrintf("Before: %08x  %s\n", pindexLast->nBits, bnOld.ToString());
    LogPrintf("After:  %08x  %s\n", bnNew.GetCompact(), bnNew.ToString());

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits)
{
    bool fNegative;
    bool fOverflow;
    uint256 bnTarget;

    if (Params().SkipProofOfWorkCheck())
       return true;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > Params().ProofOfWorkLimit())
        return error("CheckProofOfWork() : nBits below minimum work");

    // Check proof of work matches claimed amount
    if (hash > bnTarget)
        return error("CheckProofOfWork() : hash doesn't match nBits");

    return true;
}

uint256 GetBlockProof(const CBlockIndex& block)
{
    uint256 bnTarget;
    bool fNegative;
    bool fOverflow;
    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0)
        return 0;
    // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
    // as it's too large for a uint256. However, as 2**256 is at least as large
    // as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) / (bnTarget+1)) + 1,
    // or ~bnTarget / (nTarget+1) + 1.
    return (~bnTarget / (bnTarget + 1)) + 1;
}
