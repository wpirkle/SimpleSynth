#pragma once

#include "synthdefs.h"
#include "guiconstants.h"

// --- LFO may have very diff waveforms from pitched output
enum class LFOWaveform { kTriangle, kSin, kSaw, kRSH, kQRSH, kNoise, kQRNoise };


/**
\enum LFOMode

\ingroup SynthDefs

\brief LFO MOde of Operation

- kSync: LFO restarts with each new note event
- kOneShot: one cycle of LFO only
- kFreeRun: oscillator free-runs after first note-on event
*/
enum class LFOMode { kSync, kOneShot, kFreeRun };

// --- indexes in OscillatorOutputData::outputs array
enum {
	kLFONormalOutput,			// 0
	kLFONormalOutputInverted,	// 1 etc...
	kLFOQuadPhaseOutput,
	kLFOQuadPhaseOutputInverted,
	kUnipolarOutputFromMax,		//< this mimics an INVERTED EG going from MAX to MAX 
	kUnipolarOutputFromMin		/* this mimics an EG going from 0.0 -> MAX */
};

// ---
struct SynthLFOParameters
{
	SynthLFOParameters() {}
	SynthLFOParameters& operator=(const SynthLFOParameters& params)	// need this override for collections to work
	{
		if (this == &params)
			return *this;

		waveform = params.waveform;
		mode = params.mode;

		frequency_Hz = params.frequency_Hz;
		outputAmplitude = params.outputAmplitude;
		delayTime_mSec = params.delayTime_mSec;
		rampTime_mSec = params.rampTime_mSec;

		return *this;
	}

	// --- individual parameters
	LFOWaveform waveform = LFOWaveform::kQRSH;
	LFOMode mode = LFOMode::kSync;

	double frequency_Hz = 7.0;
	double outputAmplitude = 1.0;
	double delayTime_mSec = 0.0;
	double rampTime_mSec = 0.0;

};



/**
\class SynthLFO
\ingroup SynthClasses
\brief Encapsulates a synth LFO.

Outputs: contains 6 outputs

- kLFONormalOutput	
- kLFONormalOutputInverted
- kLFOQuadPhaseOutput
- kLFOQuadPhaseOutputInverted
- kUnipolarOutputFromMax
- kUnipolarOutputFromMin

*/
class SynthLFO : public ISynthModulator//ISynthOscillator
{
public:
	SynthLFO(const std::shared_ptr<MidiInputData> _midiInputData, std::shared_ptr<SynthLFOParameters> _parameters)
		: midiInputData(_midiInputData) 
	, parameters(_parameters){
		srand(time(NULL)); // --- seed random number generator

		// --- randomize the PN register
		pnRegister = rand();

	}	/* C-TOR */
	virtual ~SynthLFO() {}				/* D-TOR */

	// --- ISynthOscillator
	virtual bool reset(double _sampleRate)
	{
		sampleRate = _sampleRate;
		phaseInc = parameters->frequency_Hz / sampleRate;

		// --- timebase variables
		modCounter = 0.0;			///< modulo counter [0.0, +1.0]
		modCounterQP = 0.25;		///<Quad Phase modulo counter [0.0, +1.0]

		// --- normal reset
		delayTimer.resetTimer();
		//rampTimer.resetTimer();

		return true;
	}

	// --- ISynthModulator cont'd
	virtual bool update(bool updateAllModRoutings = true);
	virtual bool doNoteOn(double midiPitch, uint32_t _midiNoteNumber, uint32_t midiNoteVelocity) 
	{ 
		renderComplete = false;
		if (parameters->mode == LFOMode::kSync || parameters->mode == LFOMode::kOneShot)
		{
			modCounter = 0.0;			///< modulo counter [0.0, +1.0]
			modCounterQP = 0.25;		///< Quad Phase modulo counter [0.0, +1.0]
		}

		// --- reset
		delayTimer.resetTimer();
		// rampTimer.resetTimer();
		ramplitudeValue = 0.0;

		randomSHCounter = -1; // -1 = reset
		return true; 
	}

	virtual bool doNoteOff(double midiPitch, uint32_t _midiNoteNumber, uint32_t midiNoteVelocity)
	{
		return true; 
	}

	// --- the oscillator function
	const ModOutputData renderModulatorOutput();

	// --- get our modulators
	virtual std::shared_ptr<ModInputData> getModulators() {
		return modulators;
	}

	// --- not used here;
	virtual void setModulators(std::shared_ptr<ModInputData> _modulators) {
		modulators = _modulators;
	}

protected:
	// --- MIDI Data Interface
	const std::shared_ptr<MidiInputData> midiInputData = nullptr;
	
	// --- modulators
	std::shared_ptr<ModInputData> modulators = std::make_shared<ModInputData>();

	// --- parameters
	std::shared_ptr<SynthLFOParameters> parameters = nullptr;

	// --- sample rate
	double sampleRate = 0.0;			///< sample rate

	// --- timebase variables
	double modCounter = 0.0;			///< modulo counter [0.0, +1.0]
	double phaseInc = 0.0;				///< phase inc = fo/fs
	double modCounterQP = 0.25;			///< Quad Phase modulo counter [0.0, +1.0]
	bool renderComplete = false;		///< flag for one-shot

	// --- 32-bit register for RS&H
	uint32_t pnRegister = 0;			///< 32 bit register for PN oscillator
	int randomSHCounter = -1;			///< random sample/hold counter;  -1 is reset condition
	double randomSHValue = 0.0;			///< current output, needed because we hold this output for some number of samples = (sampleRate / oscFrequency)

	// --- in class stuff; this is for a fixed on-delay time
	Timer delayTimer;

	// --- in class stuff; this is the trivial way to handle the ramp time
	//     need to get them to do this as a modulation, to understand glide
	// Timer rampTimer;
	double ramplitudePerSampleStep = 0.0;
	double ramplitudeValue = 1.0;

	/**
	\struct checkAndWrapModulo
	\brief Check a modulo counter and wrap it if necessary
	*/
	inline bool checkAndWrapModulo(double& moduloCounter, double phaseInc)
	{
		// --- for positive frequencies
		if (phaseInc > 0 && moduloCounter >= 1.0)
		{
			moduloCounter -= 1.0;
			return true;
		}

		// --- for negative frequencies
		if (phaseInc < 0 && moduloCounter <= 0.0)
		{
			moduloCounter += 1.0;
			return true;
		}

		return false;
	}

	/**
	\struct checkAndWrapModulo
	\brief Advance, and then check a modulo counter and wrap it if necessary
	*/
	inline bool advanceAndCheckWrapModulo(double& moduloCounter, double phaseInc)
	{
		// --- advance counter
		moduloCounter += phaseInc;

		// --- for positive frequencies
		if (phaseInc > 0 && moduloCounter >= 1.0)
		{
			moduloCounter -= 1.0;
			return true;
		}

		// --- for negative frequencies
		if (phaseInc < 0 && moduloCounter <= 0.0)
		{
			moduloCounter += 1.0;
			return true;
		}

		return false;
	}

	// --- increment the modulo counter
	inline void advanceModulo(double& moduloCounter, double phaseInc) { moduloCounter += phaseInc; }

	// --- sine approximation with parabolas
	const double B = 4.0 / kPi;
	const double C = -4.0 / (kPi* kPi);
	const double P = 0.225;
	// http://devmaster.net/posts/9648/fast-and-accurate-sine-cosine
	//
	// --- angle is -pi to +pi
	inline double parabolicSine(double angle)
	{
		double y = B * angle + C * angle * fabs(angle);
		y = P * (y * fabs(y) - y) + y;
		return y;
	}
};

