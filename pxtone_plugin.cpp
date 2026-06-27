///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// PxTone Playback Plugin
//
// Implements RVPlaybackPlugin interface for PxTone music files using libpxtone.
// Supported formats: ptcop (PxTone Collage), pttune (PxTone Tune)
// Audio output: Stereo S16 at 48000 Hz, converted to F32 for the host.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "pxtnError.h"
#include "pxtnService.h"

extern "C" {
#include <retrovert/io.h>
#include <retrovert/log.h>
#include <retrovert/metadata.h>
#include <retrovert/playback.h>
#include <retrovert/service.h>
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define PXTONE_SAMPLE_RATE 48000
#define PXTONE_CHANNELS 2

RV_PLUGIN_USE_IO_API();
RV_PLUGIN_USE_METADATA_API();
RV_PLUGIN_USE_LOG_API();

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Memory-based I/O bridge for pxtnService

struct PxToneMemFile {
    const uint8_t* data;
    int32_t size;
    int32_t pos;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool pxtone_io_read(void* user, void* p_dst, int32_t size, int32_t num) {
    PxToneMemFile* f = (PxToneMemFile*)user;
    int32_t total = size * num;
    if (f->pos + total > f->size) {
        return false;
    }
    memcpy(p_dst, f->data + f->pos, (size_t)total);
    f->pos += total;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool pxtone_io_write(void* user, const void* p_dst, int32_t size, int32_t num) {
    (void)user;
    (void)p_dst;
    (void)size;
    (void)num;
    return false; // Read-only
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool pxtone_io_seek(void* user, int32_t mode, int32_t offset) {
    PxToneMemFile* f = (PxToneMemFile*)user;
    int32_t new_pos;
    switch (mode) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = f->pos + offset;
            break;
        case SEEK_END:
            new_pos = f->size + offset;
            break;
        default:
            return false;
    }
    if (new_pos < 0 || new_pos > f->size) {
        return false;
    }
    f->pos = new_pos;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool pxtone_io_pos(void* user, int32_t* p_pos) {
    PxToneMemFile* f = (PxToneMemFile*)user;
    *p_pos = f->pos;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct PxToneReplayerData {
    pxtnService* pxtn;
    int total_samples;
    bool finished;
    // Keep file data for seeking (reload)
    uint8_t* file_data;
    size_t file_size;
    char url[2048];
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char* pxtone_supported_extensions(void) {
    return "ptcop,pttune";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void pxtone_static_init(const RVService* service_api) {
    rv_init_log_api(service_api);
    rv_init_io_api(service_api);
    rv_init_metadata_api(service_api);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* pxtone_create(const RVService* service_api) {
    PxToneReplayerData* data = (PxToneReplayerData*)calloc(1, sizeof(PxToneReplayerData));
    if (!data) {
        return nullptr;
    }

    data->pxtn = new (std::nothrow) pxtnService(pxtone_io_read, pxtone_io_write, pxtone_io_seek, pxtone_io_pos);
    if (!data->pxtn) {
        free(data);
        return nullptr;
    }

    pxtnERR err = data->pxtn->init();
    if (err != pxtnOK) {
        rv_error("PxTone: init failed: %s", pxtnError_get_string(err));
        delete data->pxtn;
        free(data);
        return nullptr;
    }

    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int pxtone_destroy(void* user_data) {
    PxToneReplayerData* data = (PxToneReplayerData*)user_data;
    if (data->pxtn) {
        data->pxtn->clear();
        delete data->pxtn;
    }
    if (data->file_data) {
        rv_io_free_url_to_memory(data->file_data);
    }
    free(data);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVProbeResult pxtone_probe_can_play(uint8_t* probe_data, uint64_t data_size, const char* url,
                                           uint64_t total_size) {
    (void)url;
    (void)total_size;

    // Check for PxTone magic: "PTTUNE" or "PTCOLLAGE"
    if (data_size >= 9) {
        if (memcmp(probe_data, "PTCOLLAGE", 9) == 0) {
            return RVProbeResult_Supported;
        }
    }
    if (data_size >= 6) {
        if (memcmp(probe_data, "PTTUNE", 6) == 0) {
            return RVProbeResult_Supported;
        }
    }

    return RVProbeResult_Unsupported;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int pxtone_open(void* user_data, const char* url, uint32_t subsong, const RVService* service_api) {
    (void)subsong;
    (void)service_api;

    PxToneReplayerData* data = (PxToneReplayerData*)user_data;

    // Free previous file data
    if (data->file_data) {
        rv_io_free_url_to_memory(data->file_data);
        data->file_data = nullptr;
    }

    RVIoReadUrlResult read_res;
    if ((read_res = rv_io_read_url_to_memory(url)).data == nullptr) {
        rv_error("PxTone: Failed to load %s", url);
        return -1;
    }

    data->file_data = (uint8_t*)read_res.data;
    data->file_size = read_res.data_size;
    strncpy(data->url, url, sizeof(data->url) - 1);
    data->url[sizeof(data->url) - 1] = '\0';

    // Clear any previous song data
    data->pxtn->clear();

    // Set output quality
    if (!data->pxtn->set_destination_quality(PXTONE_CHANNELS, PXTONE_SAMPLE_RATE)) {
        rv_error("PxTone: Failed to set output quality");
        rv_io_free_url_to_memory(data->file_data);
        data->file_data = nullptr;
        return -1;
    }

    // Set up memory-based I/O and read the file
    PxToneMemFile memfile = { data->file_data, (int32_t)data->file_size, 0 };
    pxtnERR err = data->pxtn->read(&memfile);
    if (err != pxtnOK) {
        rv_error("PxTone: Failed to read %s: %s", url, pxtnError_get_string(err));
        data->pxtn->clear();
        rv_io_free_url_to_memory(data->file_data);
        data->file_data = nullptr;
        return -1;
    }

    // Prepare tones (instruments, delays, overdrives)
    err = data->pxtn->tones_ready();
    if (err != pxtnOK) {
        rv_error("PxTone: tones_ready failed for %s: %s", url, pxtnError_get_string(err));
        data->pxtn->clear();
        rv_io_free_url_to_memory(data->file_data);
        data->file_data = nullptr;
        return -1;
    }

    // Prepare for playback (no looping for finite playback)
    pxtnVOMITPREPARATION prep;
    memset(&prep, 0, sizeof(prep));
    prep.flags = 0; // No loop
    prep.master_volume = 1.0f;

    if (!data->pxtn->moo_preparation(&prep)) {
        rv_error("PxTone: moo_preparation failed for %s", url);
        data->pxtn->clear();
        rv_io_free_url_to_memory(data->file_data);
        data->file_data = nullptr;
        return -1;
    }

    data->total_samples = data->pxtn->moo_get_total_sample();
    data->finished = false;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void pxtone_close(void* user_data) {
    PxToneReplayerData* data = (PxToneReplayerData*)user_data;

    data->pxtn->clear();

    if (data->file_data) {
        rv_io_free_url_to_memory(data->file_data);
        data->file_data = nullptr;
    }
    data->finished = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVReadInfo pxtone_read_data(void* user_data, RVReadData dest) {
    PxToneReplayerData* data = (PxToneReplayerData*)user_data;
    RVAudioFormat format = { RVAudioStreamFormat_S16, PXTONE_CHANNELS, PXTONE_SAMPLE_RATE };

    if (data->finished || !data->pxtn->moo_is_valid_data()) {
        return (RVReadInfo) { format, 0, RVReadStatus_Finished};
    }

    uint32_t max_frames = dest.channels_output_max_bytes_size / (sizeof(int16_t) * PXTONE_CHANNELS);

    // Moo expects buffer size in bytes, outputs S16 directly to output buffer
    int32_t bytes_to_render = (int32_t)(max_frames * PXTONE_CHANNELS * sizeof(int16_t));
    bool moo_ok = data->pxtn->Moo(static_cast<int16_t*>(dest.channels_output), bytes_to_render);

    if (!moo_ok || data->pxtn->moo_is_end_vomit()) {
        data->finished = true;
        if (!moo_ok) {
            return (RVReadInfo) { format, 0, RVReadStatus_Finished};
        }
    }

    RVReadStatus status = data->finished ? RVReadStatus_Finished : RVReadStatus_Ok;
    return (RVReadInfo) { format, max_frames, status};
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int64_t pxtone_seek(void* user_data, int64_t ms) {
    PxToneReplayerData* data = (PxToneReplayerData*)user_data;

    if (!data->file_data) {
        return -1;
    }

    // PxTone doesn't support arbitrary seeking. Reset and replay.
    data->pxtn->clear();

    if (!data->pxtn->set_destination_quality(PXTONE_CHANNELS, PXTONE_SAMPLE_RATE)) {
        return -1;
    }

    PxToneMemFile memfile = { data->file_data, (int32_t)data->file_size, 0 };
    pxtnERR err = data->pxtn->read(&memfile);
    if (err != pxtnOK) {
        return -1;
    }

    err = data->pxtn->tones_ready();
    if (err != pxtnOK) {
        return -1;
    }

    pxtnVOMITPREPARATION prep;
    memset(&prep, 0, sizeof(prep));
    prep.flags = 0;
    prep.master_volume = 1.0f;
    prep.start_pos_sample = (int32_t)(ms * PXTONE_SAMPLE_RATE / 1000);

    if (!data->pxtn->moo_preparation(&prep)) {
        return -1;
    }

    data->finished = false;
    return ms;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int pxtone_metadata(const char* url, const RVService* service_api) {
    (void)service_api;

    RVIoReadUrlResult read_res;
    if ((read_res = rv_io_read_url_to_memory(url)).data == nullptr) {
        return -1;
    }

    RVMetadataId id = rv_metadata_create_url(url);
    rv_metadata_set_tag(id, RV_METADATA_SONGTYPE_TAG, "PxTone");

    // Create a temporary pxtnService to extract metadata
    pxtnService pxtn(pxtone_io_read, pxtone_io_write, pxtone_io_seek, pxtone_io_pos);
    pxtnERR err = pxtn.init();
    if (err != pxtnOK) {
        rv_io_free_url_to_memory(read_res.data);
        return -1;
    }

    pxtn.set_destination_quality(PXTONE_CHANNELS, PXTONE_SAMPLE_RATE);

    PxToneMemFile memfile = { (const uint8_t*)read_res.data, (int32_t)read_res.data_size, 0 };
    err = pxtn.read(&memfile);
    if (err == pxtnOK) {
        // Extract title
        int32_t name_size = 0;
        const char* name = pxtn.text->get_name_buf(&name_size);
        if (name && name_size > 0) {
            // Name may not be null-terminated
            char title[256] = { 0 };
            int copy_len = name_size < 255 ? name_size : 255;
            memcpy(title, name, (size_t)copy_len);
            rv_metadata_set_tag(id, RV_METADATA_TITLE_TAG, title);
        }

        // Calculate duration
        err = pxtn.tones_ready();
        if (err == pxtnOK) {
            pxtnVOMITPREPARATION prep;
            memset(&prep, 0, sizeof(prep));
            prep.master_volume = 1.0f;
            pxtn.moo_preparation(&prep);

            int32_t total = pxtn.moo_get_total_sample();
            if (total > 0) {
                double length = (double)total / (double)PXTONE_SAMPLE_RATE;
                rv_metadata_set_tag_f64(id, RV_METADATA_LENGTH_TAG, length);
            }
        }
    }

    pxtn.clear();
    rv_io_free_url_to_memory(read_res.data);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void pxtone_event(void* user_data, uint8_t* event_data, uint64_t len) {
    (void)user_data;
    (void)event_data;
    (void)len;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool pxtone_get_structure(void* user_data, RVVizInfo* out) {
    PxToneReplayerData* data = (PxToneReplayerData*)user_data;
    if (!data || !data->pxtn || !out) {
        return false;
    }
    int total = data->pxtn->Unit_Num();
    out->caps = RVVizCaps_Scope | RVVizCaps_Vu;
    out->scroll_mode = RVScrollMode_PerChannel;
    out->pattern_channel_count = 0;
    out->scope_channel_count = total > 0 ? (uint32_t)total : 0;
    out->column_count = 0;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t pxtone_get_scope_channels(void* user_data, RVChannelDesc* out, uint32_t cap) {
    PxToneReplayerData* data = (PxToneReplayerData*)user_data;
    if (!data || !data->pxtn || !out) {
        return 0;
    }
    int total = data->pxtn->Unit_Num();
    uint32_t count = total > 0 ? (uint32_t)total : 0;
    if (count > cap)
        count = cap;
    for (uint32_t i = 0; i < count; i++) {
        memset(out[i].name, 0, sizeof(out[i].name));
        snprintf((char*)out[i].name, sizeof(out[i].name), "Unit %u", i + 1);
        out[i].scope_width = 2;  // pxtone units are panned across L/R; capture is stereo
    }
    return count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void pxtone_set_scope_enabled(void* user_data, bool on) {
    PxToneReplayerData* data = (PxToneReplayerData*)user_data;
    if (!data || !data->pxtn) {
        return;
    }
    data->pxtn->moo_enable_scope_capture(on ? 1 : 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t pxtone_get_scope_samples(void* user_data, int32_t channel, float* out, uint32_t cap) {
    PxToneReplayerData* data = (PxToneReplayerData*)user_data;
    if (!data || !data->pxtn || !out) {
        return 0;
    }
    // No hidden auto-on: capture only runs when set_scope_enabled(true) was called.
    return data->pxtn->moo_get_scope_data(channel, out, cap);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t pxtone_get_vu(void* user_data, float* out, uint32_t cap) {
    PxToneReplayerData* data = (PxToneReplayerData*)user_data;
    if (!data || !data->pxtn || !out) {
        return 0;
    }
    int total = data->pxtn->Unit_Num();
    uint32_t count = total > 0 ? (uint32_t)total : 0;
    if (count > cap)
        count = cap;
    // ponytail: VU is the peak of the scope ring, so it tracks set_scope_enabled.
    float buffer[512];  // up to 256 stereo frames
    for (uint32_t ch = 0; ch < count; ch++) {
        uint32_t n = data->pxtn->moo_get_scope_data((int)ch, buffer, 512);
        float peak = 0.0f;
        for (uint32_t i = 0; i < n; i++) {
            float a = buffer[i] < 0.0f ? -buffer[i] : buffer[i];
            if (a > peak)
                peak = a;
        }
        out[ch] = peak;
    }
    return count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVPlaybackPlugin g_pxtone_plugin = {
    RV_PLAYBACK_PLUGIN_API_VERSION,
    "pxtone",
    "0.0.1",
    "libpxtone (Wohlstand)",
    pxtone_probe_can_play,
    pxtone_supported_extensions,
    pxtone_create,
    pxtone_destroy,
    pxtone_event,
    pxtone_open,
    pxtone_close,
    pxtone_read_data,
    pxtone_seek,
    pxtone_metadata,
    pxtone_static_init,
    nullptr, // settings_updated
    nullptr, // static_destroy
    pxtone_get_structure,
    nullptr, // get_columns
    nullptr, // get_pattern_channels
    pxtone_get_scope_channels,
    nullptr, // get_position
    nullptr, // get_channel_rows
    nullptr, // get_cells
    pxtone_set_scope_enabled,
    pxtone_get_scope_samples,
    pxtone_get_vu,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" RV_EXPORT RVPlaybackPlugin* rv_playback_plugin(void) {
    return &g_pxtone_plugin;
}
