/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   GenericReader.cpp
 * Author: mateus
 * 
 * Created on 14 de Abril de 2017, 12:11
 */

#include "rsyn/io/reader/GenericReader.h"
#include "rsyn/core/Rsyn.h"
#include "rsyn/io/parser/lef_def/LEFControlParser.h"
#include "rsyn/io/parser/lef_def/DEFControlParser.h"
#include "rsyn/io/parser/liberty/LibertyControlParser.h"
#include "rsyn/io/parser/sdc/SDCControlParser.h"
#include "rsyn/io/parser/verilog/SimplifiedVerilogReader.h"
#include "rsyn/io/Graphics.h"
#include "rsyn/model/timing/DefaultTimingModel.h"
#include "rsyn/model/routing/RoutingEstimator.h"
#include "rsyn/model/routing/DefaultRoutingEstimationModel.h"
#include "rsyn/model/routing/DefaultRoutingExtractionModel.h"
#include "rsyn/model/library/LibraryCharacterizer.h"
#include "rsyn/phy/PhysicalService.h"
#include "rsyn/phy/PhysicalDesign.h"
#include "rsyn/util/Stepwatch.h"
#include "rsyn/model/timing/Timer.h"
#include "rsyn/model/routing/RsttRoutingEstimatorModel.h"

namespace Rsyn {

void GenericReader::load(Engine engine, const Json& params) {
	std::string path = params.value("path", "");
		
	if (path.back() != '/')
		path += "/";

	if (!params.count("lefFiles")) {
		std::cout << "[ERROR] at least one LEF file must be specified...\n";
		return;
	} // end if

	if (params["lefFiles"].is_array()) {
		const Json fileList = params["lefFiles"];
		for (const std::string file : fileList) {
			lefFiles.push_back(path + std::string(file));
		} // end for
	} else {
		lefFiles.push_back(path + params.value("lefFiles", ""));
	} // end if

	if (!params.count("defFiles")) {
		std::cout << "[ERROR] at least one DEF file must be specified...\n";
		return;
	} // end if

	if (params["defFiles"].is_array()) {
		const Json fileList = params["defFiles"];
		for (const std::string file : fileList) {
			defFiles.push_back(path + file);
		} // end for
	} else {
		defFiles.push_back(path + params.value("defFiles", ""));
	} // end if

	if (params.count("verilogFile")) {
		verilogFile = path + params.value("verilogFile", "");
		enableNetlistFromVerilog = true;
	} // end if 

	if (params.count("sdcFile") && params.count("libFile")) {
		sdcFile = path + params.value("sdcFile", "");
		libertyFile = path + params.value("libFile", "");

		enableTiming = true;

		localWireResistancePerMicron =
			engine.getSessionVariableAsFloat("localWireCapacitancePerMicron", 2.535f);
		localWireCapacitancePerMicron = (Number)
			std::atof(engine.getSessionVariableAsString("localWireResistancePerMicron", "0.16E-15f").c_str());
		maxWireSegmentInMicron =
			engine.getSessionVariableAsInteger("maxWireSegmentInMicron", 100);
		enableRSTT = engine.getSessionVariableAsBool("enableRSTT");

		std::cout << "\n";
		std::cout << std::left << std::setw(40) << "Technology parameter";
		std::cout << "Value\n";
		std::cout << std::left << std::setw(40) << "localWireCapacitancePerMicron...";
		std::cout << localWireCapacitancePerMicron << "\n";
		std::cout << std::left << std::setw(40) << "localWireResistancePerMicron...";
		std::cout << localWireResistancePerMicron << "\n";
		std::cout << std::left << std::setw(40) << "maxWireSegmentInMicron...";
		std::cout << maxWireSegmentInMicron << "\n";
		std::cout << std::left << std::setw(40) << "enableRSTT...";
		std::cout << std::boolalpha << enableRSTT << "\n";
		std::cout << "\n";

		enableTiming = true;
	} // end if 

	this->engine = engine;

	parsingFlow();


} // end method

// -----------------------------------------------------------------------------

void GenericReader::parsingFlow() {
	Stepwatch watch("Running generic reader");

	parseLEFFiles();

	parseDEFFiles();

	if (enableNetlistFromVerilog)
		parseVerilogFile();

	if (enableTiming)
		parseLibertyFile();

	populateDesign();

	if (enableTiming)
		parseSDCFile();

	initializeAuxiliarInfrastructure();
} // end method 

// -----------------------------------------------------------------------------

void GenericReader::parseLEFFiles() {
	Stepwatch watch("Parsing LEF files");
	LEFControlParser lefParser;

	for (int i = 0; i < lefFiles.size(); i++) {
		if (!boost::filesystem::exists(lefFiles[i])) {
			std::cout << "[WARNING] Failed to open file " << lefFiles[i] << "\n";
			std::exit(1);
		} // end if 

		lefParser.parseLEF(lefFiles[i], lefDescriptor);
	} // end for
} // end method 

// -----------------------------------------------------------------------------

void GenericReader::parseDEFFiles() {
	Stepwatch watch("Parsing DEF files");
	DEFControlParser defParser;

	for (int i = 0; i < defFiles.size(); i++) {
		if (!boost::filesystem::exists(defFiles[i])) {
			std::cout << "[WARNING] Failed to open file " << defFiles[i] << "\n";
			std::exit(1);
		} // end if 

		defParser.parseDEF(defFiles[i], defDescriptor);
	} // end for
} // end method

// -----------------------------------------------------------------------------

void GenericReader::parseVerilogFile() {
	Stepwatch watch("Parsing Verilog file");

	if (!boost::filesystem::exists(verilogFile)) {
		std::cout << "[WARNING] Failed to open file " << verilogFile << "\n";
		std::exit(1);
	} // end if 

	Parsing::SimplifiedVerilogReader parser(verilogDescriptor);
	parser.parseFromFile(verilogFile);
} // end method 

// -----------------------------------------------------------------------------

void GenericReader::populateDesign() {
	Stepwatch watch("Populating the design");

	Rsyn::Design design = engine.getDesign();

	if (enableTiming)
		Reader::populateRsynLibraryFromLiberty(libInfo, design);

	if (enableNetlistFromVerilog)
		Reader::populateRsyn(lefDescriptor, defDescriptor, verilogDescriptor, design);
	else Reader::populateRsyn(lefDescriptor, defDescriptor, design);

	Json physicalDesignConfiguration;
	physicalDesignConfiguration["clsEnableMergeRectangles"] = true;
	physicalDesignConfiguration["clsEnableNetPinBoundaries"] = true;
	physicalDesignConfiguration["clsEnableRowSegments"] = true;
	engine.startService("rsyn.physical", physicalDesignConfiguration);
	Rsyn::PhysicalService* phService = engine.getService("rsyn.physical");
	Rsyn::PhysicalDesign physicalDesign = phService->getPhysicalDesign();
	physicalDesign.loadLibrary(lefDescriptor);
	physicalDesign.loadDesign(defDescriptor);
	physicalDesign.updateAllNetBounds(false);
} // end method

// -----------------------------------------------------------------------------

void GenericReader::parseLibertyFile() {
	Stepwatch parsingLibertyWatch("Parsing Liberty file");

	Rsyn::Design design = engine.getDesign();

	if (!boost::filesystem::exists(libertyFile)) {
		std::cout << "[WARNING] Failed to open file " << libertyFile << "\n";
		std::exit(1);
	} // end if 

	LibertyControlParser libertyParser;
	libertyParser.parseLiberty(libertyFile, libInfo);
} // end method 

// -----------------------------------------------------------------------------

void GenericReader::parseSDCFile() {
	Stepwatch watchParsingSdc("Parsing SDC file");
	SDCControlParser sdcParser;

	if (!boost::filesystem::exists(sdcFile)) {
		std::cout << "[WARNING] Failed to open file " << sdcFile << "\n";
		std::exit(1);
	} // end if 

	sdcParser.parseSDC_iccad15(sdcFile, sdcInfo);
	watchParsingSdc.finish();
} // end method

// -----------------------------------------------------------------------------

void GenericReader::initializeAuxiliarInfrastructure() {
	Rsyn::Design design = engine.getDesign();

	if (enableTiming) {
		Stepwatch watchScenario("Loading scenario");
		engine.startService("rsyn.scenario",{});
		Rsyn::Scenario* scenario = engine.getService("rsyn.scenario");
		scenario->init(design, libInfo, libInfo, sdcInfo);
		watchScenario.finish();

		
		RoutingEstimationModel* routingEstimationModel;
		
		if (!enableRSTT) {
			engine.startService("rsyn.defaultRoutingEstimationModel",{});
			DefaultRoutingEstimationModel* defaultRoutingEstimationModel =
				engine.getService("rsyn.defaultRoutingEstimationModel");
			routingEstimationModel = defaultRoutingEstimationModel;
		} else {
			engine.startService("rsyn.RSTTroutingEstimationModel");
			RsttRoutingEstimatorModel* rsstRoutingEstimationModel = 
				engine.getService("rsyn.RSTTroutingEstimationModel");
			routingEstimationModel = rsstRoutingEstimationModel;
		} // end if

		engine.startService("rsyn.defaultRoutingExtractionModel",{});
		DefaultRoutingExtractionModel* routingExtractionModel =
			engine.getService("rsyn.defaultRoutingExtractionModel");

		const Number resPerMicron = (Number) Rsyn::Units::convertToInternalUnits(
			Rsyn::MEASURE_RESISTANCE, localWireResistancePerMicron, Rsyn::NO_UNIT_PREFIX);

		const Number capPerMicron = (Number) Rsyn::Units::convertToInternalUnits(
			Rsyn::MEASURE_CAPACITANCE, localWireCapacitancePerMicron, Rsyn::NO_UNIT_PREFIX);

		DBU clsDesignDistanceUnit = (DBU) lefDescriptor.clsLefUnitsDscp.clsDatabase;

		routingExtractionModel->initialize(
			resPerMicron / clsDesignDistanceUnit,
			capPerMicron / clsDesignDistanceUnit,
			maxWireSegmentInMicron * clsDesignDistanceUnit);

		engine.startService("rsyn.routingEstimator",{});
		RoutingEstimator *routingEstimator = engine.getService("rsyn.routingEstimator");
		Stepwatch updateSteiner("Updating Steiner trees");
		routingEstimator->setRoutingEstimationModel(routingEstimationModel);
		routingEstimator->setRoutingExtractionModel(routingExtractionModel);
		routingEstimator->updateRoutingFull();
		updateSteiner.finish();

		engine.startService("rsyn.timer",{});
		Rsyn::Timer* timer = engine.getService("rsyn.timer");

		Stepwatch watchInit("Initializing timer");
		timer->init(
			design,
			engine,
			scenario,
			libInfo,
			libInfo);
		watchInit.finish();

		Stepwatch watchInitModel("Initializing default timing model");
		engine.startService("rsyn.defaultTimingModel",{});
		DefaultTimingModel* timingModel = engine.getService("rsyn.defaultTimingModel");
		timer->setTimingModel(timingModel);
		watchInitModel.finish();

		Stepwatch watchInitLogicalEffort("Library characterization");
		engine.startService("rsyn.libraryCharacterizer",{});
		LibraryCharacterizer *libc = engine.getService("rsyn.libraryCharacterizer");
		libc->runLibraryAnalysis(design, timingModel);
		watchInitLogicalEffort.finish();

		Stepwatch updateTiming("Updating timing");
		timer->updateTimingFull();
		updateTiming.finish();

		engine.startService("rsyn.report",{});
	} // end if

	// Start graphics service...
	engine.startService("rsyn.graphics",{});
	Graphics *graphics = engine.getService("rsyn.graphics");
	graphics->coloringByCellType();
	//graphics->coloringRandomGray();

	// Start writer service...
	engine.startService("rsyn.writer",{});

	// Start jezz service..
	engine.startService("rsyn.jezz",{});
} // end method

// -----------------------------------------------------------------------------

//void GenericReader::parseInputFiles() {
//	LEFControlParser lefParser;
//	DEFControlParser defParser;
//
//	Legacy::Design verilogDesignDescriptor;
//	LefDscp lefDscp;
//	DefDscp defDscp;
//
//	// Start parsing...
//	Stepwatch watchParsing("Parsing Design");
//
//	std::setlocale(LC_ALL, "en_US.UTF-8");
//	lefParser.parseLEF(lefFiles[0], lefDscp);
//	defParser.parseDEF(defFiles, defDscp);
//	
//	Parsing::SimplifiedVerilogReader parser(verilogDesignDescriptor);
//	parser.parseFromFile(verilogFile);
//	
//	watchParsing.finish();
//	// Parsing complete...
//  	
//	Stepwatch watchRsyn("Populating Rsyn");
//	Rsyn::Design design = engine.getDesign();
//
//	ISPD13::LIBInfo libInfo;
//	if (enableTiming) {
//		LibertyControlParser libParser;
//	
//		Stepwatch watchParsingLibertyEarly("Parsing Liberty");
//		libParser.parseLiberty(libFile, libInfo);
//		watchParsingLibertyEarly.finish();
//		
//		Stepwatch watchLibrary("Loading library into Rsyn");
//		Reader::populateRsynLibraryFromLiberty(libInfo, design);
//		watchLibrary.finish();
//	} // end if 
//	
//	Reader::populateRsyn(
//		lefDscp,
//		defDscp,
//		verilogDesignDescriptor,
//		design);
//	watchRsyn.finish();
//	
//	// Start physical design...
//	Stepwatch watchPopulateLayers("Initializing Physical Layer");
//	Json physicalDesignConfiguratioon;
//	physicalDesignConfiguratioon["clsEnableMergeRectangles"] = true;
//	physicalDesignConfiguratioon["clsEnableNetPinBoundaries"] = true;
//	physicalDesignConfiguratioon["clsEnableRowSegments"] = true;
//	engine.startService("rsyn.physical", physicalDesignConfiguratioon);		
//	Rsyn::PhysicalService* phService = engine.getService("rsyn.physical");
//	Rsyn::PhysicalDesign physicalDesign = phService->getPhysicalDesign();
//	physicalDesign.loadLibrary(lefDscp);
//	physicalDesign.loadDesign(defDscp);
//	physicalDesign.updateAllNetBounds(false);
//	watchPopulateLayers.finish();
//	// Physical design complete...
//	
//	if (enableTiming) {
//		SDCControlParser sdcParser;
//		ISPD13::SDCInfo sdcInfo;
//		
//		Stepwatch watchParsingSdc("Parsing SDC");
//		sdcParser.parseSDC_iccad15(sdcFile, sdcInfo);
//		watchParsingSdc.finish();
//		
//		Stepwatch watchScenario("Loading scenario");
//		engine.startService("rsyn.scenario", {});
//		Rsyn::Scenario* scenario = engine.getService("rsyn.scenario");
//		scenario->init(design, libInfo, libInfo, sdcInfo);
//		watchScenario.finish();
//		
//		engine.startService("rsyn.defaultRoutingEstimationModel", {});
//		DefaultRoutingEstimationModel* routingEstimationModel =
//			engine.getService("rsyn.defaultRoutingEstimationModel");
//
//		engine.startService("rsyn.defaultRoutingExtractionModel", {});
//		DefaultRoutingExtractionModel* routingExtractionModel = 
//			engine.getService("rsyn.defaultRoutingExtractionModel");
//	
//		const Number resPerMicron = (Number) Rsyn::Units::convertToInternalUnits(
//				Rsyn::MEASURE_RESISTANCE, localWireResistancePerMicron, Rsyn::NO_UNIT_PREFIX);
//
//		const Number capPerMicron = (Number) Rsyn::Units::convertToInternalUnits(
//				Rsyn::MEASURE_CAPACITANCE, localWireCapacitancePerMicron, Rsyn::NO_UNIT_PREFIX);
//
//		DBU clsDesignDistanceUnit = (DBU)lefDscp.clsLefUnitsDscp.clsDatabase;
//
//		routingExtractionModel->initialize(
//				resPerMicron / clsDesignDistanceUnit,
//				capPerMicron / clsDesignDistanceUnit,
//				maxWireSegmentInMicron * clsDesignDistanceUnit);
//
//		engine.startService("rsyn.routingEstimator", {});
//		RoutingEstimator *routingEstimator = engine.getService("rsyn.routingEstimator");
//		Stepwatch updateSteiner("Updating Steiner trees");
//		routingEstimator->setRoutingEstimationModel(routingEstimationModel);
//		routingEstimator->setRoutingExtractionModel(routingExtractionModel);
//		routingEstimator->updateRoutingFull();
//		updateSteiner.finish();	
//		
//		engine.startService("rsyn.timer", {});		
//		Rsyn::Timer* timer = engine.getService("rsyn.timer");
//
//		Stepwatch watchInit("Initializing timer");
//		timer->init(
//				design,
//				engine,
//				scenario,
//				libInfo,
//				libInfo);
//		watchInit.finish();
//
//		Stepwatch watchInitModel("Initializing default timing model");
//		engine.startService("rsyn.defaultTimingModel", {});
//		DefaultTimingModel* timingModel = engine.getService("rsyn.defaultTimingModel");
//		timer->setTimingModel(timingModel);
//		watchInitModel.finish();
//
//		Stepwatch watchInitLogicalEffort("Library characterization");
//		engine.startService("rsyn.libraryCharacterizer", {});
//		LibraryCharacterizer *libc = engine.getService("rsyn.libraryCharacterizer");
//		libc->runLibraryAnalysis(design, timingModel);
//		watchInitLogicalEffort.finish();
//
//		Stepwatch updateTiming("Updating timing");
//		timer->updateTimingFull();
//		updateTiming.finish();
//
//		engine.startService("rsyn.report", {});	
//	} // end if
//	
//	// Start graphics service...
//	engine.startService("rsyn.graphics", {});
//	Graphics *graphics = engine.getService("rsyn.graphics");
//	graphics->coloringByCellType();
//	// Graphics service complete...
//
//	// Start writer service...
//	engine.startService("rsyn.writer", {});	
//	// Writer service complete...
//	
//	// Start jezz service..
//	engine.startService("rsyn.jezz", {});
//	// Jezz complete
//} // end method

} // end namespace 