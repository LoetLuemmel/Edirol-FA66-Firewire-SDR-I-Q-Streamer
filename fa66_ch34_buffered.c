#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <CoreAudio/CoreAudio.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreFoundation/CoreFoundation.h>

#define PORT 1234
#define START_CHANNEL 2
#define RING_BUFFER_SIZE (1024 * 1024)

typedef struct {
    uint8_t data[RING_BUFFER_SIZE];
    volatile uint32_t write_pos;
    volatile uint32_t read_pos;
    pthread_mutex_t mutex;
} RingBuffer;

RingBuffer ring_buffer;
int client_fd = -1;
int use_buffer = 1;
int sample_rate = 48000;  // Default: 48 kHz
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

void ring_buffer_init(RingBuffer *rb) {
    memset(rb->data, 0, RING_BUFFER_SIZE);
    rb->write_pos = 0;
    rb->read_pos = 0;
    pthread_mutex_init(&rb->mutex, NULL);
}

uint32_t ring_buffer_available(RingBuffer *rb) {
    uint32_t w = rb->write_pos;
    uint32_t r = rb->read_pos;
    if (w >= r) {
        return w - r;
    } else {
        return RING_BUFFER_SIZE - r + w;
    }
}

void ring_buffer_write(RingBuffer *rb, uint8_t *data, uint32_t len) {
    pthread_mutex_lock(&rb->mutex);
    
    for (uint32_t i = 0; i < len; i++) {
        rb->data[rb->write_pos] = data[i];
        rb->write_pos = (rb->write_pos + 1) % RING_BUFFER_SIZE;
        
        if (rb->write_pos == rb->read_pos) {
            rb->read_pos = (rb->read_pos + 1) % RING_BUFFER_SIZE;
        }
    }
    
    pthread_mutex_unlock(&rb->mutex);
}

uint32_t ring_buffer_read(RingBuffer *rb, uint8_t *data, uint32_t len) {
    pthread_mutex_lock(&rb->mutex);
    
    uint32_t available = ring_buffer_available(rb);
    uint32_t to_read = (len < available) ? len : available;
    
    for (uint32_t i = 0; i < to_read; i++) {
        data[i] = rb->data[rb->read_pos];
        rb->read_pos = (rb->read_pos + 1) % RING_BUFFER_SIZE;
    }
    
    pthread_mutex_unlock(&rb->mutex);
    return to_read;
}

OSStatus inputCallback(void *inRefCon,
                      AudioUnitRenderActionFlags *ioActionFlags,
                      const AudioTimeStamp *inTimeStamp,
                      UInt32 inBusNumber,
                      UInt32 inNumberFrames,
                      AudioBufferList *ioData) {
    
    AudioUnit audioUnit = (AudioUnit)inRefCon;
    
    AudioBufferList bufferList;
    bufferList.mNumberBuffers = 1;
    bufferList.mBuffers[0].mNumberChannels = 6;
    bufferList.mBuffers[0].mDataByteSize = inNumberFrames * 6 * sizeof(int16_t);
    bufferList.mBuffers[0].mData = malloc(bufferList.mBuffers[0].mDataByteSize);
    
    OSStatus status = AudioUnitRender(audioUnit, ioActionFlags, inTimeStamp,
                                     inBusNumber, inNumberFrames, &bufferList);
    
    if (status == noErr) {
        int16_t *samples = (int16_t *)bufferList.mBuffers[0].mData;
        uint8_t *iq_data = malloc(inNumberFrames * 2);
        
        for (UInt32 i = 0; i < inNumberFrames; i++) {
            int16_t ch3 = samples[i * 6 + START_CHANNEL];
            int16_t ch4 = samples[i * 6 + START_CHANNEL + 1];
            
            iq_data[i * 2] = (uint8_t)((ch3 + 32768) >> 8);
            iq_data[i * 2 + 1] = (uint8_t)((ch4 + 32768) >> 8);
        }
        
        if (use_buffer) {
            ring_buffer_write(&ring_buffer, iq_data, inNumberFrames * 2);
        } else {
            pthread_mutex_lock(&client_mutex);
            if (client_fd >= 0) {
                send(client_fd, iq_data, inNumberFrames * 2, MSG_DONTWAIT);
            }
            pthread_mutex_unlock(&client_mutex);
        }
        
        free(iq_data);
    }
    
    free(bufferList.mBuffers[0].mData);
    return noErr;
}

void *sender_thread(void *arg) {
    uint8_t send_buffer[8192];
    
    while (use_buffer) {
        pthread_mutex_lock(&client_mutex);
        
        if (client_fd >= 0) {
            uint32_t available = ring_buffer_available(&ring_buffer);
            
            if (available >= sizeof(send_buffer)) {
                uint32_t read = ring_buffer_read(&ring_buffer, send_buffer, sizeof(send_buffer));
                
                if (read > 0) {
                    ssize_t sent = send(client_fd, send_buffer, read, MSG_DONTWAIT);
                    if (sent < 0) {
                        close(client_fd);
                        client_fd = -1;
                        printf("Client disconnected\n");
                    }
                }
            }
        }
        
        pthread_mutex_unlock(&client_mutex);
        usleep(1000);
    }
    
    return NULL;
}

void *command_handler(void *arg) {
    uint8_t cmd[5];
    while (client_fd >= 0) {
        int n = recv(client_fd, cmd, 5, 0);
        if (n <= 0) {
            pthread_mutex_lock(&client_mutex);
            close(client_fd);
            client_fd = -1;
            pthread_mutex_unlock(&client_mutex);
            break;
        }
    }
    return NULL;
}

AudioDeviceID FindFA66Device() {
    AudioDeviceID deviceID = kAudioDeviceUnknown;
    UInt32 propertySize;
    
    AudioObjectPropertyAddress propertyAddress = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMaster
    };
    
    AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &propertyAddress, 0, NULL, &propertySize);
    int deviceCount = propertySize / sizeof(AudioDeviceID);
    AudioDeviceID *devices = malloc(propertySize);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, NULL, &propertySize, devices);
    
    for (int i = 0; i < deviceCount; i++) {
        CFStringRef deviceName;
        propertySize = sizeof(deviceName);
        propertyAddress.mSelector = kAudioDevicePropertyDeviceNameCFString;
        AudioObjectGetPropertyData(devices[i], &propertyAddress, 0, NULL, &propertySize, &deviceName);
        
        char name[256];
        CFStringGetCString(deviceName, name, sizeof(name), kCFStringEncodingUTF8);
        
        if (strstr(name, "FA-66") != NULL) {
            deviceID = devices[i];
            printf("Found: %s (ID: %d)\n", name, deviceID);
        }
        CFRelease(deviceName);
    }
    
    free(devices);
    return deviceID;
}

void print_usage(const char *prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("FA-66 I/Q to rtl_tcp network server\n");
    printf("Streams audio channels 3+4 from Edirol FA-66 as I/Q data\n\n");
    printf("Options:\n");
    printf("  -r, --rate <Hz>  Sample rate (default: 48000)\n");
    printf("                   Valid: 44100, 48000, 96000, 192000\n");
    printf("  -b, --buffer     Enable ring buffer (default, stable)\n");
    printf("  -n, --nobuffer   Disable ring buffer (low latency mode)\n");
    printf("  -h, --help       Show this help\n\n");
    printf("Examples:\n");
    printf("  %s                      # 48 kHz with buffer\n", prog);
    printf("  %s -r 96000             # 96 kHz with buffer\n", prog);
    printf("  %s -r 192000 -n         # 192 kHz without buffer\n\n", prog);
    printf("Use in GQRX:\n");
    printf("  Device string: rtl_tcp=127.0.0.1:1234\n");
    printf("  Input rate: Same as sample rate (e.g., 48000)\n");
}

int validate_sample_rate(int rate) {
    int valid_rates[] = {44100, 48000, 96000, 192000};
    for (int i = 0; i < 4; i++) {
        if (rate == valid_rates[i]) {
            return 1;
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--buffer") == 0) {
            use_buffer = 1;
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--nobuffer") == 0) {
            use_buffer = 0;
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--rate") == 0) {
            if (i + 1 < argc) {
                sample_rate = atoi(argv[++i]);
                if (!validate_sample_rate(sample_rate)) {
                    printf("ERROR: Invalid sample rate: %d\n", sample_rate);
                    printf("Valid rates: 44100, 48000, 96000, 192000\n");
                    return 1;
                }
            } else {
                printf("ERROR: --rate requires an argument\n");
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            printf("ERROR: Unknown option: %s\n\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    ring_buffer_init(&ring_buffer);
    
    AudioDeviceID deviceID = FindFA66Device();
    
    if (deviceID == kAudioDeviceUnknown) {
        printf("ERROR: FA-66 not found!\n");
        return 1;
    }
    
    AudioComponentDescription desc;
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_HALOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;
    
    AudioComponent component = AudioComponentFindNext(NULL, &desc);
    AudioUnit audioUnit;
    AudioComponentInstanceNew(component, &audioUnit);
    
    UInt32 enableIO = 1;
    AudioUnitSetProperty(audioUnit, kAudioOutputUnitProperty_EnableIO,
                        kAudioUnitScope_Input, 1, &enableIO, sizeof(enableIO));
    
    enableIO = 0;
    AudioUnitSetProperty(audioUnit, kAudioOutputUnitProperty_EnableIO,
                        kAudioUnitScope_Output, 0, &enableIO, sizeof(enableIO));
    
    AudioUnitSetProperty(audioUnit, kAudioOutputUnitProperty_CurrentDevice,
                        kAudioUnitScope_Global, 0, &deviceID, sizeof(deviceID));
    
    AudioStreamBasicDescription format;
    format.mSampleRate = sample_rate;  // Verwende gewählte Sample-Rate
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    format.mBitsPerChannel = 16;
    format.mChannelsPerFrame = 6;
    format.mBytesPerFrame = 12;
    format.mFramesPerPacket = 1;
    format.mBytesPerPacket = 12;
    
    AudioUnitSetProperty(audioUnit, kAudioUnitProperty_StreamFormat,
                        kAudioUnitScope_Output, 1, &format, sizeof(format));
    
    AURenderCallbackStruct callbackStruct;
    callbackStruct.inputProc = inputCallback;
    callbackStruct.inputProcRefCon = audioUnit;
    
    AudioUnitSetProperty(audioUnit, kAudioOutputUnitProperty_SetInputCallback,
                        kAudioUnitScope_Global, 0, &callbackStruct, sizeof(callbackStruct));
    
    AudioUnitInitialize(audioUnit);
    
    if (use_buffer) {
        pthread_t sender_tid;
        pthread_create(&sender_tid, NULL, sender_thread, NULL);
    }
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 1);
    
    printf("\n=== FA-66 I/Q Server ===\n");
    printf("Port: %d\n", PORT);
    printf("Sample rate: %d Hz\n", sample_rate);
    printf("Channels: %d and %d (I/Q)\n", START_CHANNEL + 1, START_CHANNEL + 2);
    printf("Buffer mode: %s\n", use_buffer ? "ENABLED (stable)" : "DISABLED (low latency)");
    if (use_buffer) {
        printf("Ring buffer: %d KB\n", RING_BUFFER_SIZE / 1024);
    }
    printf("\nGQRX settings:\n");
    printf("  Device string: rtl_tcp=127.0.0.1:1234\n");
    printf("  Input rate: %d\n", sample_rate);
    printf("\nWaiting for connection...\n");
    
    AudioOutputUnitStart(audioUnit);
    
    while (1) {
        pthread_mutex_lock(&client_mutex);
        if (client_fd < 0) {
            pthread_mutex_unlock(&client_mutex);
            
            client_fd = accept(server_fd, NULL, NULL);
            printf("Client connected!\n");
            
            pthread_t cmd_thread;
            pthread_create(&cmd_thread, NULL, command_handler, NULL);
        } else {
            pthread_mutex_unlock(&client_mutex);
        }
        
        sleep(1);
    }
    
    return 0;
}
