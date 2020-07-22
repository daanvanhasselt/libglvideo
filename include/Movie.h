#pragma once

#include <string>
#include <map>
#include <array>
#include <thread>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include "Frame.h"
#include "TrackDescription.h"
#include "Context.h"
#include "Decoder.h"
#include "concurrency.h"

class AP4_File;
class AP4_Track;

namespace glvideo {


/// Type alias to use for any representation of time.
typedef double seconds;

class UnsupportedCodecError : public std::runtime_error {
public:
    UnsupportedCodecError( const std::string &what ) : std::runtime_error( what ) {}
};

/// \class Movie
/// \brief Plays a movie from a file.
class Movie {
public:
    /// Options for a Movie
    class Options {
    public:
        Options() {}

        /// Set whether the movie should pre-buffer frames on load. Defaults to
        /// true, but it's useful to disable if you're not going to play the
        /// movie before seeking or doing something else that would invalidate
        /// the buffer anyway.
        Options &prebuffer( bool prebuffer ) { m_prebuffer = prebuffer; return *this; }
        bool prebuffer() const { return m_prebuffer; }
        bool &prebuffer() { return m_prebuffer; }

        /// Set the number of \a frames to be buffered in CPU memory by the
        /// movie. Higher values result in smoother playback, but use more
        /// CPU memory.
        Options &cpuBufferSize( size_t frames ) { m_cpuBufferSize = frames; return *this; }
        size_t cpuBufferSize() const { return m_cpuBufferSize; }
        size_t &cpuBufferSize() { return m_cpuBufferSize; }

        /// Set the number of \a frames to be buffered in GPU memory by the
        /// movie. Higher values result in smoother playback, but use more
        /// GPU memory. Any value lower than 2 may cause playback issues.
        Options &gpuBufferSize( size_t frames ) { m_gpuBufferSize = frames; return *this; }
        size_t gpuBufferSize() const { return m_gpuBufferSize; }
        size_t &gpuBufferSize() { return m_gpuBufferSize; }

    private:
        size_t m_cpuBufferSize = 2;
        size_t m_gpuBufferSize = 2;
        bool m_prebuffer = true;
    };


    typedef std::shared_ptr<Movie> ref;
    typedef std::chrono::high_resolution_clock clock;

    /// Returns a ref to a movie constructed from a source \a filename.
    static ref
    create( const Context::ref &context, const std::string &filename, const Options &options = Options() )
    {
        return ref( new Movie( context, filename, options ));
    }

    /// Returns a ref to a movie constructed from another movie.
    static ref
    create( const Movie & original )
    {
        return ref( new Movie( original ) );
    }

    /// Constructs a movie from a \a texContext, and a source \a filename.
    ///
    /// @param  texContext  a shared GL context that will be used to create textures on a separate thread.
    /// @param  filename    the filename to read from.
    ///
    /// \throws Error if the file cannot be read
    Movie( const Context::ref &context, const std::string &filename, const Options &options = Options() );

    ~Movie();

    /// Constructs a new movie with the same parameters as the original
    explicit Movie( const Movie& original );

    Movie& operator=( const Movie& ) = delete;

    /// Returns the filename passed during construction.
    std::string getFilename() const { return m_filename; }

    /// Returns a string representation of the container format (eg. "qt 512").
    std::string getFormat() const;

    /// Returns a string representation of the primary video track's codec
    std::string getCodec() const { return m_codec; }

    /// Returns the number of tracks found in the container.
    size_t getNumTracks() const;

    /// Returns the TrackDescription for the track at \a index. Indexes start at 0, and are internally mapped to track ids.
    TrackDescription getTrackDescription( size_t index ) const;

    /// Returns the length of the movie in seconds.
    seconds getDuration() const;

    /// Returns the position of the playhead in seconds.
    seconds getElapsedTime() const;

	/// Returns the remaining amount of time in seconds.
	seconds getRemainingTime() const;

    /// Returns the framerate
    float getFramerate() const { return m_fps; }

    /// Returns the width
    uint32_t getWidth() const { return m_width; }

    /// Returns the height
    uint32_t getHeight() const { return m_height; }

    /// Starts playing the movie.
    Movie & play();

    /// Is the movie playing?
    bool isPlaying() const { return m_isPlaying; }

    /// Stops playing the movie, terminating the read thread.
    Movie & stop();

    /// Pauses playing the movie, but leaves the read thread running. Same as setting the playback rate to 0.
    Movie & pause();

	/// Set whether the movie should automatically loop when it reaches the end (chainable).
	Movie & loop( bool loop = true ) { m_loop = loop; return *this; }

	/// Set the playhead to the beginning of the video.
    Movie & seekToStart();

    /// Set the playhead to a given \a time. Times greater than the movie
    /// length will wrap.
    Movie & seek( seconds time );

    /// Set the playhead to a given \a sample number.
    Movie & seekToSample( size_t sample );

    /// Get the playback rate as a multiple of the native framerate.
    float getPlaybackRate() const { return m_playbackRate; };

    /// Set the playback rate to a multiple of its native framerate.
    Movie & setPlaybackRate( float rate );

    /// Returns the current Frame.
    FrameTexture::ref getCurrentFrame() const;

    /// Call this in the application update method.
    /// Buffers frames to the GPU in the current OpenGL context and queues the
    /// next frame.
    void update();

    /// Fill buffers with samples.
    void prebuffer();

private:
    std::string getTrackCodec( size_t index ) const;
    std::string getTrackCodec( AP4_Track * track ) const;


    std::string m_filename;
    AP4_File *m_file = NULL;
    std::map<size_t, uint32_t> m_trackIndexMap;
    Options m_options;
    FrameTexture::ref m_currentFrame = nullptr;
    Context::ref m_context = nullptr;
    AP4_Track * m_videoTrack = nullptr;
    float m_fps;
    std::chrono::duration< float > m_spf;
    double m_fpf;
    float m_playbackRate = 1.f;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    std::string m_codec;
	size_t m_numSamples = 0;
	size_t m_currentSample = 0;
    std::vector< GLuint > m_pbos;
    size_t m_currentPBO = 0;
    size_t m_id = 0;
    bool m_forceRefreshCurrentFrame = false;

    /// Extracts and decodes the sample with index \a i_sample from track with index \a i_track and returns a Frame.
    Frame::ref getFrame( AP4_Track * track, size_t i_sample ) const;
    std::unique_ptr< Decoder > m_decoder;

    /// Reads frames into the frame buffer on a thread, and queues them.
	void queueRead();
	void read();
	void waitForJobsToFinish();
    void doPrebufferWork();
    void bufferNextCPUSample();
    void bufferNextGPUSample();

	std::atomic_bool m_isPlaying{ false };
	std::atomic_bool m_jobsPending{ false };
	std::condition_variable m_jobsPendingCV;
	std::mutex m_jobsMutex;
	std::atomic_bool m_loop{ false };
	//std::atomic< size_t > m_readSample{ 0 };
    std::atomic< double > m_readFraction = 0.;
    void incrementReadFraction(double increment) {
            auto current = m_readFraction.load();
            while (!m_readFraction.compare_exchange_weak(current, current + increment))
                ;
    }
    size_t getReadSample() const { return (size_t)((double)m_readFraction * (double)m_numSamples); }

	concurrent_buffer< Frame::ref > m_cpuFrameBuffer;
    concurrent_buffer< Frame::ref > m_gpuFrameBuffer;
	clock::time_point m_lastFrameQueuedAt;
};
}
