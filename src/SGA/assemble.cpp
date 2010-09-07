//-----------------------------------------------
// Copyright 2009 Wellcome Trust Sanger Institute
// Written by Jared Simpson (js18@sanger.ac.uk)
// Released under the GPL
//-----------------------------------------------
//
// assemble - convert read overlaps into contigs
//
#include <iostream>
#include <fstream>
#include "Util.h"
#include "assemble.h"
#include "SGUtil.h"
#include "SGAlgorithms.h"
#include "SGPairedAlgorithms.h"
#include "SGDebugAlgorithms.h"
#include "SGVisitors.h"
#include "Timer.h"
#include "EncodedString.h"

//
// Getopt
//
#define SUBPROGRAM "assemble"
static const char *ASSEMBLE_VERSION_MESSAGE =
SUBPROGRAM " Version " PACKAGE_VERSION "\n"
"Written by Jared Simpson.\n"
"\n"
"Copyright 2009 Wellcome Trust Sanger Institute\n";

static const char *ASSEMBLE_USAGE_MESSAGE =
"Usage: " PACKAGE_NAME " " SUBPROGRAM " [OPTION] ... ASQGFILE\n"
"Create contigs from the assembly graph ASQGFILE.\n"
"\n"
"  -v, --verbose                        display verbose output\n"
"      --help                           display this help and exit\n"
"      -o, --out=FILE                   write the contigs to FILE (default: contigs.fa)\n"
"      -m, --min-overlap=LEN            only use overlaps of at least LEN. This can be used to filter\n"
"                                       the overlap set so that the overlap step only needs to be run once.\n"
"      -b, --bubble=N                   perform N bubble removal steps\n"
"      -s, --smooth                     perform variation smoothing algorithm\n"
"      -t, --trim=N                     trim terminal branches using N rounds\n"
"      -r,--resolve-small=LEN           resolve small repeats using spanning overlaps when the difference between the shortest\n"
"                                       and longest overlap is greater than LEN\n"
"      -a, --asqg-outfile=FILE          write the final graph to FILE\n"
"\nReport bugs to " PACKAGE_BUGREPORT "\n\n";

namespace opt
{
    static unsigned int verbose;
    static std::string asqgFile;
    static std::string prefix;
    static std::string outFile;
    static std::string debugFile;
    static std::string asqgOutfile;
    static unsigned int minOverlap;
    static bool bEdgeStats = false;
    static bool bCorrectReads = false;
    static bool bRemodelGraph = false;
    static bool bSmoothGraph = false;
    static int resolveSmallRepeatLen = -1;
    static int  numTrimRounds = 0;
    static int  numBubbleRounds = 0;
    static bool bValidate;
    static bool bExact = false;
}

static const char* shortopts = "p:o:m:d:t:b:a:r:svc";

enum { OPT_HELP = 1, OPT_VERSION, OPT_VALIDATE };

static const struct option longopts[] = {
    { "verbose",        no_argument,       NULL, 'v' },
    { "prefix",         required_argument, NULL, 'p' },
    { "out",            required_argument, NULL, 'o' },
    { "min-overlap",    required_argument, NULL, 'm' },
    { "debug-file",     required_argument, NULL, 'd' },
    { "bubble",         required_argument, NULL, 'b' },
    { "trim",           required_argument, NULL, 't' },
    { "asqg-outfile",   required_argument, NULL, 'a' },
    { "resolve-small",  required_argument, NULL, 'r' },    
    { "smooth",         no_argument,       NULL, 's' },    
    { "correct",        no_argument,       NULL, 'c' },    
    { "remodel",        no_argument,       NULL, 'z' },
    { "edge-stats",     no_argument,       NULL, 'x' },
    { "exact",          no_argument,       NULL, 'e' },
    { "help",           no_argument,       NULL, OPT_HELP },
    { "version",        no_argument,       NULL, OPT_VERSION },
    { "validate",       no_argument,       NULL, OPT_VALIDATE},
    { NULL, 0, NULL, 0 }
};

//
// Main
//
int assembleMain(int argc, char** argv)
{
    Timer* pTimer = new Timer("sga assemble");
    parseAssembleOptions(argc, argv);
    assemble();
    delete pTimer;

    return 0;
}

void assemble()
{
    Timer t("sga assemble");
    StringGraph* pGraph = SGUtil::loadASQG(opt::asqgFile, opt::minOverlap, true);
    if(opt::bExact)
        pGraph->setExactMode(true);
    pGraph->printMemSize();

    // Visitor functors
    SGTransitiveReductionVisitor trVisit;
    SGGraphStatsVisitor statsVisit;
    SGRemodelVisitor remodelVisit;
    SGEdgeStatsVisitor edgeStatsVisit;
    SGTrimVisitor trimVisit;
    SGBubbleVisitor bubbleVisit;
    SGBubbleEdgeVisitor bubbleEdgeVisit;

    SGContainRemoveVisitor containVisit;
    SGErrorCorrectVisitor errorCorrectVisit;
    SGValidateStructureVisitor validationVisit;
    SGPairedPathResolveVisitor peResolveVisit;

    if(!opt::debugFile.empty())
    {
        // Pre-assembly graph stats
        std::cout << "Initial graph stats\n";
        pGraph->visit(statsVisit);

        SGDebugGraphCompareVisitor* pDebugGraphVisit = new SGDebugGraphCompareVisitor(opt::debugFile);
        
        /*
        pGraph->visit(*pDebugGraphVisit);
        while(pGraph->visit(realignVisitor))
            pGraph->visit(*pDebugGraphVisit);
        SGOverlapWriterVisitor overlapWriter("final-overlaps.ovr");
        pGraph->visit(overlapWriter);
        */
        //pDebugGraphVisit->m_showMissing = true;
        pGraph->visit(*pDebugGraphVisit);
        pGraph->visit(statsVisit);
        delete pDebugGraphVisit;
        //return;
    }

    if(opt::bEdgeStats)
    {
        std::cout << "Computing edge stats\n";
        pGraph->visit(edgeStatsVisit);
    }

    // Pre-assembly graph stats
    std::cout << "Initial graph stats\n";
    pGraph->visit(statsVisit);    

    // Remove containments from the graph
    std::cout << "Removing contained vertices\n";
    while(pGraph->hasContainment())
    {
        pGraph->visit(containVisit);
    }

    // Pre-assembly graph stats
    std::cout << "Post-contain graph stats\n";
    pGraph->visit(statsVisit);    

    // Remove transitive edges from the graph
    std::cout << "Removing transitive edges\n";
    pGraph->visit(trVisit);

    // Resolve PE paths
    //pGraph->visit(peResolveVisit);

    if(opt::bValidate)
    {
        std::cout << "Validating graph structure\n";
        pGraph->visit(validationVisit);
    }

    //std::cout << "Writing graph file\n";
    //pGraph->writeASQG("afterTR.asqg.gz");

    std::cout << "Pre-remodelling graph stats\n";
    pGraph->visit(statsVisit);

    if(opt::bCorrectReads)
    {
        std::cout << "Correcting reads\n";
        pGraph->visit(errorCorrectVisit);

        std::cout << "Writing corrected reads\n";
        SGFastaVisitor correctedVisitor("correctedReads.fa");
        pGraph->visit(correctedVisitor);
        pGraph->writeASQG("afterEC.asqg.gz");
    }

    if(opt::bRemodelGraph)
    {
        // Remodel graph
        std::cout << "Remodelling graph\n";
        pGraph->visit(remodelVisit);
        pGraph->writeASQG("afterRM.asqg.gz");

        while(pGraph->hasContainment())
        {
            std::cout << "Removing contained reads\n";
            pGraph->visit(containVisit);
        }
        pGraph->visit(trVisit);
        std::cout << "After remodel graph stats: \n";
        pGraph->visit(statsVisit);
        //pGraph->writeASQG("afterRM.asqg.gz");
    }

    if(opt::numTrimRounds > 0)
    {
        WARN_ONCE("USING NAIVE TRIMMING");
        std::cout << "Trimming bad vertices\n"; 
        int numTrims = opt::numTrimRounds;
        while(numTrims-- > 0)
           pGraph->visit(trimVisit);
    }

    if(opt::bSmoothGraph)
    {
        std::cout << "\nPerforming variation smoothing\n";
        int numSmooth = 4;
        SGSmoothingVisitor smoothingVisit;
        while(numSmooth-- > 0)
            pGraph->visit(smoothingVisit);
        //pGraph->visit(trimVisit);
        //pGraph->simplify();
    }

    if(opt::resolveSmallRepeatLen >= 0)
    {
        SGSmallRepeatResolveVisitor smallRepeatVisit(opt::resolveSmallRepeatLen);
        std::cout << "Resolving small repeats\n";

        while(pGraph->visit(smallRepeatVisit)) {}
        
        std::cout << "After small repeat resolve graph stats\n";
        pGraph->visit(statsVisit);
    }
    
    pGraph->writeASQG("postmod.asqg.gz");

/*
    if(opt::numBubbleRounds > 0)
    {
        std::cout << "Removing bubble edges\n";
        while(pGraph->visit(bubbleEdgeVisit)) {}
        pGraph->visit(trimVisit);
    }
*/
    // Simplify the graph by compacting edges
    std::cout << "Pre-simplify graph stats\n";
    pGraph->visit(statsVisit);

    /*
    SGBreakWriteVisitor breakWriter("breaks.txt");
    pGraph->visit(breakWriter);
    pGraph->writeASQG("postmod.asqg.gz");
    */
    pGraph->simplify();
    
    if(opt::numBubbleRounds > 0)
    {
        std::cout << "\nPerforming bubble removal\n";
        // Bubble removal
        int numPops = opt::numBubbleRounds;
        while(numPops-- > 0)
            pGraph->visit(bubbleVisit);
        pGraph->simplify();
    }
    
    std::cout << "\nFinal graph stats\n";
    pGraph->visit(statsVisit);

#ifdef VALIDATE
    VALIDATION_WARNING("SGA/assemble")
    pGraph->validate();
#endif

    // Rename the vertices to have contig IDs instead of read IDs
    pGraph->renameVertices("contig-");

    // Write the results
    pGraph->writeDot("final.dot");
    //pGraph->writeASQG("final.asqg");
    SGFastaVisitor av(opt::outFile);
    pGraph->visit(av);
    if(!opt::asqgOutfile.empty())
    {
        pGraph->writeASQG(opt::asqgOutfile);
    }
    delete pGraph;
}

// 
// Handle command line arguments
//
void parseAssembleOptions(int argc, char** argv)
{
    // Set defaults
    opt::outFile = "contigs.fa";
    opt::minOverlap = 0;

    bool die = false;
    for (char c; (c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1;) 
    {
        std::istringstream arg(optarg != NULL ? optarg : "");
        switch (c) 
        {
            case 'p': arg >> opt::prefix; break;
            case 'o': arg >> opt::outFile; break;
            case 'm': arg >> opt::minOverlap; break;
            case 'd': arg >> opt::debugFile; break;
            case '?': die = true; break;
            case 'v': opt::verbose++; break;
            case 'b': arg >> opt::numBubbleRounds; break;
            case 's': opt::bSmoothGraph = true; break;
            case 't': arg >> opt::numTrimRounds; break;
            case 'a': arg >> opt::asqgOutfile; break;
            case 'c': opt::bCorrectReads = true; break;
            case 'r': arg >> opt::resolveSmallRepeatLen; break;
            case 'z': opt::bRemodelGraph = true; break;
            case 'x': opt::bEdgeStats = true; break;
            case 'e': opt::bExact = true; break;
            case OPT_VALIDATE: opt::bValidate = true; break;
            case OPT_HELP:
                std::cout << ASSEMBLE_USAGE_MESSAGE;
                exit(EXIT_SUCCESS);
            case OPT_VERSION:
                std::cout << ASSEMBLE_VERSION_MESSAGE;
                exit(EXIT_SUCCESS);
                
        }
    }

    if (argc - optind < 1) 
{
        std::cerr << SUBPROGRAM ": missing arguments\n";
        die = true;
    } 
    else if (argc - optind > 1) 
    {
        std::cerr << SUBPROGRAM ": too many arguments\n";
        die = true;
    }

    if (die) 
    {
        std::cerr << "Try `" << SUBPROGRAM << " --help' for more information.\n";
        exit(EXIT_FAILURE);
    }

    // Parse the input filename
    opt::asqgFile = argv[optind++];
}
