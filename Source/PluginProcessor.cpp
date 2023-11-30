/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
ChorusFlangerAudioProcessor::ChorusFlangerAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
    /** construct and add parameters */
    addParameter(mDryWetParameter = new juce::AudioParameterFloat("drywet",
                                                                  "Dry Wet",
                                                                  0.0,
                                                                  1.0,
                                                                  0.5));
    addParameter(mDepthParameter = new juce::AudioParameterFloat("depth",
                                                                  "Depth",
                                                                  0.0,
                                                                  1.0,
                                                                  0.5));
    addParameter(mRateParameter = new juce::AudioParameterFloat("rate",
                                                                  "Rate",
                                                                  0.1f,
                                                                  20.f,
                                                                  10.f));
    addParameter(mPhaseOffsetParameter = new juce::AudioParameterFloat("phaseoffset",
                                                                  "Phase Offset",
                                                                  0.0f,
                                                                  1.f,
                                                                  0.f));
    
    addParameter(mFeedbackParameter = new juce::AudioParameterFloat("feedback",
                                                                    "Feedback",
                                                                    0,
                                                                    0.98,
                                                                    0.5));
    
    addParameter(mTypeParameter = new juce::AudioParameterInt("type",
                                                        "Type",
                                                        0,
                                                        1,
                                                        0));
    /** initialize data to default values */
    mCircularBufferLeft = nullptr;
    mCircularBufferRight = nullptr;
    mCircularBufferWriteHead = 0;
    mCircularBufferLength = 0;
    
    mFeedbackLeft = 0;
    mFeedbackRight = 0;
    mLFOPhase = 0;
}

ChorusFlangerAudioProcessor::~ChorusFlangerAudioProcessor()
{
    if (mCircularBufferLeft != nullptr){
        delete [] mCircularBufferLeft;
        mCircularBufferLeft = nullptr;
    }
    
    if (mCircularBufferRight != nullptr){
        delete [] mCircularBufferRight;
        mCircularBufferRight = nullptr;
    }
}

//==============================================================================
const juce::String ChorusFlangerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool ChorusFlangerAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool ChorusFlangerAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool ChorusFlangerAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double ChorusFlangerAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int ChorusFlangerAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int ChorusFlangerAudioProcessor::getCurrentProgram()
{
    return 0;
}

void ChorusFlangerAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String ChorusFlangerAudioProcessor::getProgramName (int index)
{
    return {};
}

void ChorusFlangerAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void ChorusFlangerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    /** initialize our data for the current sample rate , and reset things such as phase and writeheadds */
    
    /** initialize the phase*/
    mLFOPhase = 0;
    
    /** calculate circular buffer length */
    mCircularBufferLength = sampleRate * MAX_DELAY_TIME;
    
    /** initialize the left buffer */
    if (mCircularBufferLeft == nullptr) {
        mCircularBufferLeft = new float[mCircularBufferLength];
    }
    
    /** clear any junk data in the new buffer */
    juce::zeromem(mCircularBufferLeft, mCircularBufferLength * sizeof(float));
    
    /** initialize the right buffer */
    if (mCircularBufferRight == nullptr) {
        mCircularBufferRight = new float[mCircularBufferLength];
    }
    
    /** clear any junk data in the right buffer*/
    juce::zeromem(mCircularBufferRight, mCircularBufferLength * sizeof(float));
    
    /** initialize the write head to 0 */
    mCircularBufferWriteHead = 0;
}

void ChorusFlangerAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool ChorusFlangerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void ChorusFlangerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    /** obtain the left adn right audio data pointers */
    float* leftChannel = buffer.getWritePointer(0);
    float* rightChannel = buffer.getWritePointer(1);

    /** iterate through all the samples in the buffer */
    for (int i = 0; i < buffer.getNumSamples(); i++) {
        /** write into circular buffer */
        mCircularBufferLeft[mCircularBufferWriteHead] = leftChannel[i] + mFeedbackLeft;
        mCircularBufferRight[mCircularBufferWriteHead] = rightChannel[i] + mFeedbackRight;
        
        /** generate the left lfo output */
        float lfoOutLeft = sin(2*M_PI * mLFOPhase);
        
        /** calculate the right channel lfo phase */
        float lfoPhaseRight = mLFOPhase + *mPhaseOffsetParameter;

        if (lfoPhaseRight > 1) {
            lfoPhaseRight -= 1;
        }
        
        /** generate right channel lfo output */
        float lfoOutRight = sin(2*M_PI * lfoPhaseRight);
        
        /** move lfo phase forward*/
        mLFOPhase += *mRateParameter / getSampleRate();
        
        if (mLFOPhase > 1) {
            mLFOPhase -= 1;
        }
        
        /** control lfo depth */
        lfoOutLeft *= *mDepthParameter;
        lfoOutRight *= *mDepthParameter;
        
        /** map lfo output to desired delay times */
        float lfoOutMappedLeft = 0;
        float lfoOutMappedRight = 0;
        
        /** chorus*/
        if (*mTypeParameter == 0) {
            lfoOutMappedLeft = juce::jmap(lfoOutLeft, -1.0f, 1.0f, 0.005f, 0.03f);
            lfoOutMappedRight = juce::jmap(lfoOutRight, -1.f, 1.f, 0.005f, 0.03f);
        /** flanger**/
        } else {
            lfoOutMappedLeft = juce::jmap(lfoOutLeft, -1.0f, 1.0f, 0.001f, 0.005f);
            lfoOutMappedRight = juce::jmap(lfoOutRight, -1.f, 1.f, 0.001f, 0.005f);
        }
       
        /** calculate delay lengths in samples */
        float delayTimeSamplesLeft = getSampleRate() * lfoOutMappedLeft;
        float delayTimeSamplesRight = getSampleRate() * lfoOutMappedRight;
        
        /** calculate the left read head position*/
        float delayReadHeadLeft = mCircularBufferWriteHead - delayTimeSamplesLeft;
        
        if (delayReadHeadLeft < 0) {
            delayReadHeadLeft += mCircularBufferLength;
        }
        
        /** calculate the right read head position*/
        float delayReadHeadRight = mCircularBufferWriteHead - delayTimeSamplesRight;
        
        if (delayReadHeadRight < 0) {
            delayReadHeadRight += mCircularBufferLength;
        }
        
        /** calculate linear interpolation points for left channel*/
        int readHeadLeft_x = (int)delayReadHeadLeft;
        int readHeadLeft_x1 = readHeadLeft_x + 1;
        float readHeadFloatLeft = delayReadHeadLeft - readHeadLeft_x;

        if (readHeadLeft_x1 >=mCircularBufferLength) {
            readHeadLeft_x1 -= mCircularBufferLength;
        }
        
        /** calculate linear interpolation points for right channel */
        int readHeadRight_x = (int)delayReadHeadRight;
        int readHeadRight_x1 = readHeadRight_x + 1;
        float readHeadFloatRight = delayReadHeadRight - readHeadRight_x;
        
        if (readHeadRight_x1 >= mCircularBufferLength) {
            readHeadRight_x1 -= mCircularBufferLength;
        }
                
        /** generate left and right output samples */
        float delay_sample_left = lin_interp(mCircularBufferLeft[readHeadLeft_x], mCircularBufferLeft[readHeadLeft_x1], readHeadFloatLeft);
        float delay_sample_right = lin_interp(mCircularBufferRight[readHeadRight_x], mCircularBufferRight[readHeadRight_x1], readHeadFloatRight);
        
        mFeedbackLeft = delay_sample_left * *mFeedbackParameter;
        mFeedbackRight = delay_sample_right * *mFeedbackParameter;
        
        mCircularBufferWriteHead++;
        
        if (mCircularBufferWriteHead >= mCircularBufferLength) {
            mCircularBufferWriteHead = 0;
        }
        
        float dryAmount = 1 - *mDryWetParameter;
        float wetAmount = *mDryWetParameter;
        
        buffer.setSample(0, i, buffer.getSample(0, i) * dryAmount + delay_sample_left * wetAmount);
        buffer.setSample(1, i, buffer.getSample(1, i) * dryAmount + delay_sample_left * wetAmount);
    }
}

//==============================================================================
bool ChorusFlangerAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* ChorusFlangerAudioProcessor::createEditor()
{
    return new ChorusFlangerAudioProcessorEditor (*this);
}

//==============================================================================
void ChorusFlangerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    std::unique_ptr<juce::XmlElement> xml( new juce::XmlElement("FlangerChorus"));
    
    xml->setAttribute("DryWet", *mDryWetParameter);
    xml->setAttribute("Depth", *mDepthParameter);
    xml->setAttribute("Rate", *mRateParameter);
    xml->setAttribute("PhaseOffset", *mPhaseOffsetParameter);
    xml->setAttribute("Feedback", *mFeedbackParameter);
    xml->setAttribute("Type", *mTypeParameter);
    
    copyXmlToBinary(*xml, destData);
}

void ChorusFlangerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    
    if (xml.get() != nullptr && xml->hasTagName("FlangerChorus"))
    {
        *mDryWetParameter = xml->getDoubleAttribute("DryWet");
        *mDepthParameter = xml->getDoubleAttribute("Depth");
        *mRateParameter = xml->getDoubleAttribute("Rate");
        *mPhaseOffsetParameter = xml->getDoubleAttribute("PhaseOffset");
        *mFeedbackParameter = xml->getDoubleAttribute("Feedback");
        
        *mTypeParameter = xml->getIntAttribute("Type");
    }

}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChorusFlangerAudioProcessor();
}

float ChorusFlangerAudioProcessor::lin_interp(float sample_x, float sample_x1, float inPhase)
{
    return (1-inPhase) * sample_x + inPhase * sample_x1;
}
