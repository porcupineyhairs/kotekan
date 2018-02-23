#include "processFactory.hpp"

#include "errors.h"
#include "processes/beamformingPostProcess.hpp"

#include "beamformingPostProcess.hpp"
#include "chrxUplink.hpp"
#include "computeDualpolPower.hpp"
#ifdef WITH_DPDK
    #include "dpdkWrapper.hpp"
#endif
#include "fullPacketDump.hpp"
#include "gpuPostProcess.hpp"
#include "frbPostProcess.hpp"
#include "pulsarPostProcess.hpp"
#include "nDiskFileWrite.hpp"
#include "nDiskFileRead.hpp"
#include "networkPowerStream.hpp"
#include "pyPlotResult.hpp"
#include "rawFileRead.hpp"
#include "rawFileWrite.hpp"
#include "vdifStream.hpp"
#include "recvSingleDishVDIF.hpp"
#include "streamSingleDishVDIF.hpp"
#include "networkInputPowerStream.hpp"
#include "integratePowerStream.hpp"
#include "bufferStatus.hpp"
#include "gpuBeamformSimulate.hpp"
#include "gpuBeamformPulsarSimulate.hpp"
#include "gpuSimulate.hpp"
#include "networkOutputSim.hpp"
#include "simVdifData.hpp"
#include "testDataCheck.hpp"
#include "testDataGen.hpp"
#include "constDataCheck.hpp"
#include "accumulate.hpp"
#include "hexDump.hpp"
#include "chimeMetadataDump.hpp"
#include "bufferSend.hpp"
#include "bufferRecv.hpp"
#include "simpleAutocorr.hpp"
#include "fakeVis.hpp"
#include "freqSlicer.hpp"
#include "stripXProd.hpp"
#include "baselineSubset.hpp"
#include "fakeGpuBuffer.hpp"
#include "rfiVDIF.hpp"
#include "rfiBroadcastVDIF.hpp"
#ifndef MAC_OSX
#include "frbNetworkProcess.hpp"
#include "pulsarNetworkProcess.hpp"
#endif
#include "frbPostProcess_in.hpp"

#ifdef WITH_HDF5
    #include "visWriter.hpp"
#endif
#ifdef WITH_HSA
    #include "hsaProcess.hpp"
#endif
#ifdef WITH_OPENCL
    #include "clProcess.hpp"
#endif
#ifdef WITH_AIRSPY
    #include "airspyInput.hpp"
#endif
#ifdef WITH_FFTW
    #include "fftwEngine.hpp"
#endif

processFactory::processFactory(Config& config,
                               bufferContainer& buffer_container) :
    config(config),
    buffer_container(buffer_container) {

}

processFactory::~processFactory() {
}

map<string, KotekanProcess *> processFactory::build_processes() {
    map<string, KotekanProcess *> processes;

    // Start parsing tree, put the processes in the "processes" vector
    build_from_tree(processes, config.get_full_config_json(), "");

    return processes;
}

void processFactory::build_from_tree(map<string, KotekanProcess *>& processes, json& config_tree, const string& path) {

    for (json::iterator it = config_tree.begin(); it != config_tree.end(); ++it) {
        // If the item isn't an object we can just ignore it.
        if (!it.value().is_object()) {
            continue;
        }

        // Check if this is a kotekan_process block, and if so create the process.
        string process_name = it.value().value("kotekan_process", "none");
        if (process_name != "none") {
            string unique_name = path + "/" + it.key();
            if (processes.count(unique_name) != 0) {
                throw std::runtime_error("A process with the path " + unique_name + " has been defined more than once!");
            }
            processes[unique_name] = new_process(process_name, path + "/" + it.key());
            continue;
        }

        // Recursive part.
        // This is a section/scope not a process block.
        build_from_tree(processes, it.value(), path + "/" + it.key());
    }
}


KotekanProcess* processFactory::new_process(const string& name, const string& location) {
    // I wish there was a better way to do this...  The only thing I can think of
    // involves the compiler preprocessor.

    INFO("Creating process type: %s, at config tree path: %s", name.c_str(), location.c_str());

    // ****** processes directory ******
    if (name == "accumulate") {
        return (KotekanProcess *) new accumulate(config, location, buffer_container);
    }

    if (name == "beamformingPostProcess") {
        return (KotekanProcess *) new beamformingPostProcess(config, location, buffer_container);
    }

    if (name == "chrxUplink") {
        return (KotekanProcess *) new chrxUplink(config, location, buffer_container);
    }

    if (name == "computeDualpolPower") {
        return (KotekanProcess *) new computeDualpolPower(config, location, buffer_container);
    }
    if (name == "bufferStatus") {
        return (KotekanProcess *) new bufferStatus(config, location, buffer_container);
    }
#ifdef WITH_DPDK
    if (name == "dpdkWrapper") {
        return (KotekanProcess *) new dpdkWrapper(config, location, buffer_container);
    }
#endif
    if (name == "fullPacketDump") {
        return (KotekanProcess *) new fullPacketDump(config, location, buffer_container);
    }

    if (name == "hexDump") {
        return (KotekanProcess *) new hexDump(config, location, buffer_container);
    }

    if (name == "gpuPostProcess") {
        return (KotekanProcess *) new gpuPostProcess(config, location, buffer_container);
    }
    if (name == "frbPostProcess") {
        return (KotekanProcess *) new frbPostProcess(config, location, buffer_container);
    }
    if (name == "pulsarPostProcess") {
        return (KotekanProcess *) new pulsarPostProcess(config, location, buffer_container);
    }
#ifndef MAC_OSX
    if (name == "frbNetworkProcess") {
        return (KotekanProcess *) new frbNetworkProcess(config, location, buffer_container);
    }
    if (name == "pulsarNetworkProcess") {
        return (KotekanProcess *) new pulsarNetworkProcess(config, location, buffer_container);
    }
#endif
    if (name == "frbPostProcess_in") {
        return (KotekanProcess *) new frbPostProcess_in(config, location, buffer_container);
    }

    if (name == "nDiskFileWrite") {
        return (KotekanProcess *) new nDiskFileWrite(config, location, buffer_container);
    }

    if (name == "nDiskFileRead") {
        return (KotekanProcess *) new nDiskFileRead(config, location, buffer_container);
    }

    if (name == "networkPowerStream") {
        return (KotekanProcess *) new networkPowerStream(config, location, buffer_container);
    }

    if (name == "integratePowerStream") {
        return (KotekanProcess *) new integratePowerStream(config, location, buffer_container);
    }

    if (name == "networkInputPowerStream") {
        return (KotekanProcess *) new networkInputPowerStream(config, location, buffer_container);
    }

    if (name == "pyPlotResult") {
        return (KotekanProcess *) new pyPlotResult(config, location, buffer_container);
    }

    if (name == "rawFileRead") {
        return (KotekanProcess *) new rawFileRead(config, location, buffer_container);
    }

    if (name == "rawFileWrite") {
        return (KotekanProcess *) new rawFileWrite(config, location, buffer_container);
    }

    if (name == "vdifStream") {
        return (KotekanProcess *) new vdifStream(config, location, buffer_container);
    }

    if (name == "rfiVDIF") {
        return (KotekanProcess *) new rfiVDIF(config, location, buffer_container);
    }

    if (name == "rfiBroadcastVDIF") {
        return (KotekanProcess *) new rfiBroadcastVDIF(config, location, buffer_container);
    }

#ifdef WITH_AIRSPY
    if (name == "airspyInput") {
        return (KotekanProcess *) new airspyInput(config, location, buffer_container);
    }
#endif

#ifdef WITH_FFTW
    if (name == "fftwEngine") {
        return (KotekanProcess *) new fftwEngine(config, location, buffer_container);
    }
#endif

    if (name == "simpleAutocorr") {
        return (KotekanProcess *) new simpleAutocorr(config, location, buffer_container);
    }

    if (name == "streamSingleDishVDIF") {
        return (KotekanProcess *) new streamSingleDishVDIF(config, location, buffer_container);
    }

    if (name == "recvSingleDishVDIF") {
        return (KotekanProcess *) new recvSingleDishVDIF(config, location, buffer_container);
    }

    // ****** testing directory ******
    if (name == "gpuBeamformSimulate") {
        return (KotekanProcess *) new gpuBeamformSimulate(config, location, buffer_container);
    }
    if (name == "gpuBeamformPulsarSimulate") {
        return (KotekanProcess *) new gpuBeamformPulsarSimulate(config, location, buffer_container);
    }

    if (name == "gpuSimulate") {
        return (KotekanProcess *) new gpuSimulate(config, location, buffer_container);
    }

    if (name == "networkOutputSim") {
        return (KotekanProcess *) new networkOutputSim(config, location, buffer_container);
    }

    if (name == "simVdifData") {
        return (KotekanProcess *) new simVdifData(config, location, buffer_container);
    }

    if (name == "testDataCheck") {
        // TODO This is a template class, how to set template type?
        return (KotekanProcess *) new testDataCheck<int>(config, location, buffer_container);
    }
    if (name == "testDataCheckFloat") {
        return (KotekanProcess *) new testDataCheck<float>(config, location, buffer_container);
    }
    if (name == "testDataCheckUchar") {
        return (KotekanProcess *) new testDataCheck<unsigned char>(config, location, buffer_container);
    }

    if (name == "constDataCheck") {
        // TODO This is a template class, how to set template type?
        return (KotekanProcess *) new constDataCheck(config, location, buffer_container);
    }

    if (name == "testDataGen") {
        return (KotekanProcess *) new testDataGen(config, location, buffer_container);
    }

    if (name == "chimeMetadataDump") {
        return (KotekanProcess *) new chimeMetadataDump(config, location, buffer_container);
    }

    // HDF5
    if (name == "visWriter") {
        #ifdef WITH_HDF5
            return (KotekanProcess *) new visWriter(config, location, buffer_container);
        #else
            throw std::runtime_error("hdf5Writer is not supported on this system");
        #endif
    }

    // vis processes
    if (name == "visTransform") {
        return (KotekanProcess *) new visTransform(config, location, buffer_container);
    }
    if (name == "visDebug") {
        return (KotekanProcess *) new visDebug(config, location, buffer_container);
    }

    // Generate fake visbilities
    if (name == "fakeVis") {
        return (KotekanProcess *) new fakeVis(config, location, buffer_container);
    }

    // Strip buffer of cross products
    if (name == "stripXProd") {
        return (KotekanProcess *) new stripXProd(config, location, buffer_container);
    }

    // Split frequencies
    if (name == "freqSplit") {
        return (KotekanProcess *) new freqSplit(config, location, buffer_container);
    }

    // Subset frequencies
    if (name == "freqSubset") {
        return (KotekanProcess *) new freqSubset(config, location, buffer_container);
    }

    if (name == "baselineSubset") {
        return (KotekanProcess *) new baselineSubset(config, location, buffer_container);
    }

    // Generate fake visbilities in GPU buffer format
    if (name == "fakeGpuBuffer") {
        return (KotekanProcess *) new fakeGpuBuffer(config, location, buffer_container);
    }

    if (name == "bufferSend") {
        return (KotekanProcess *) new bufferSend(config, location, buffer_container);
    }

    if (name == "bufferRecv") {
        return (KotekanProcess *) new bufferRecv(config, location, buffer_container);
    }

    // OpenCL
    if (name == "clProcess") {
        #ifdef WITH_OPENCL
            return (KotekanProcess *) new clProcess(config, location, buffer_container);
        #else
            throw std::runtime_error("hsaProcess is not supported on this system");
        #endif
    }

    // HSA
    if (name == "hsaProcess") {
        #ifdef WITH_HSA
            return (KotekanProcess *) new hsaProcess(config, location, buffer_container);
        #else
            throw std::runtime_error("hsaProcess is not supported on this system");
        #endif
    }

    // No process found
    throw std::runtime_error("No process named " + name);
}
