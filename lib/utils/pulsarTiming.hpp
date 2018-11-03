
#ifndef PULSAR_TIMING_HPP
#define PULSAR_TIMING_HPP

#include <vector>
#include "time.h"


/**
 * @class Polyco
 * @brief Calculate pulsar timing given polynomial expansion of phase.
 *
 * Based on code from Tara and Carol's summer project.
 *
 * @author Tristan Pinsonneault-Marotte
 **/
class Polyco {

public:
    /**
     * @brief Constructor.
     * @param tmid       Reference time in MJD (days).
     * @param dm         Dispersion measure (cm^-3 pc).
     * @param phase_ref  Reference phase at tmid.
     * @param rot_freq   Rotation frequency (Hz), i.e. (period)^-1.
     * @param coeff      List of polynomial coefficients in Tempo format.
     **/
    Polyco(double tmid, float dm, double phase_ref, double rot_freq, std::vector<float> coeff);

    Polyco();

    /**
     * @brief Calculate pulsar phase from time in MJD.
     * @param t    Time in MJD (days).
     * @returns    Phase in number of rotations since reference.
     **/
    double mjd2phase(double t);

    /**
     * @brief Calculate pulsar phase from UNIX time.
     * @param t    timespec.
     * @returns    Phase in number of rotations since reference.
     **/
    double unix2phase(timespec t);

    /**
     * @brief Calculate next pulse time of arrival.
     * @param t      timespec.
     * @param freq   The frequency (MHz) to use for dispersion delay.
     * @returns      Time after t in seconds.
     **/
    double next_toa(timespec t, float freq);

private:
    double tmid;
    float dm;
    double phase_ref;
    double rot_freq;
    std::vector<float> coeff;

};

/**
 * @brief Add an offset to a timespec.
 * @param t     timespec to modify.
 * @param nsec  Number of nsec to add (can be negative).
 * @returns     Modified timespec.
 **/
timespec add_nsec(timespec t, long nsec);

#endif