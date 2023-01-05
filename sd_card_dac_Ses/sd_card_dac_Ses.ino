

#include "SD.h"
#include "FS.h"
#include <driver/i2s.h>
#include <driver/dac.h>
#include "sesler.h"
#include <BluetoothSerial.h>
#include <string.h>

// Digital I/O used
#define SD_CS 32
#define SPI_MOSI 13  // SD Card
#define SPI_MISO 12
#define SPI_SCK 14

// WAV file path
char* path[100];
bool playing_ = false;
int file_counter = 0;

/* --- Dynamic list --- */

struct comp {
  // Имя файла (8.3 File name)
  char file_name[100];
  // Для директории "true"
  bool is_dir = true;
  // Размер
  uint32_t fd_size = 0;
  // Ссылка на следущий элемент списка
  comp* next;
};

struct dyn_list {
  // Первый элемент
  comp* head;
  // Последний элемент
  comp* tail;
};

/** Создание пустого списка */
void constr_list(dyn_list& l) {
  l.head = NULL;
}

/** Проверка списка на отсутствие элементов ("true" для пустого списка). */
bool is_empty(dyn_list l) {
  return l.head == NULL;
}

/** Включение в список нового элемента. */
void add(dyn_list& list, const char* o_name, bool o_dir, float o_size) {
  comp* c = new comp();
  strcpy(c->file_name, o_name);
  c->is_dir = o_dir;
  c->fd_size = o_size;
  c->next = NULL;
  if (is_empty(list)) list.head = c;
  else list.tail->next = c;
  list.tail = c;
}


/* --- Playback WAV --- */

#define CCCC(c1, c2, c3, c4) ((c4 << 24) | (c3 << 16) | (c2 << 8) | c1)

/* Data structures to process wav file */
typedef enum headerState_e {
  HEADER_RIFF,
  HEADER_FMT,
  HEADER_DATA,
  DATA
} headerState_t;

typedef struct wavRiff_s {
  uint32_t chunkID;
  uint32_t chunkSize;
  uint32_t format;
} wavRiff_t;

typedef struct wavProperties_s {
  uint32_t chunkID;
  uint32_t chunkSize;
  uint16_t audioFormat;
  uint16_t numChannels;
  uint32_t sampleRate;
  uint32_t byteRate;
  uint16_t blockAlign;
  uint16_t bitsPerSample;
} wavProperties_t;

/* --- I2S configuration --- */

// I2S port number
const int i2s_num = 0;
const i2s_config_t i2s_config = {
  .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN),
  .sample_rate = 44100,
  // DAC module will only take the 8 bits from MSB
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
  .communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_I2S_MSB,
  // default interrupt priority: 0
  // high interrupt priority: ESP_INTR_FLAG_LEVEL1
  .intr_alloc_flags = 0,
  .dma_buf_count = 8,
  .dma_buf_len = 64,
  .use_apll = false
};

bool isAvailableSD = false;

BluetoothSerial SerialBT;

File RootDir;

uint32_t* data_amp;

void setup() {
  Serial.begin(115200);
  SerialBT.begin("Akilli_Stetoskop");
  Serial.print("Initializing SD card... ");
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  if (!SD.begin(SD_CS)) {
    Serial.println("failed!");
    return;
  }
  Serial.println("done.");// Root directory
  dacGreeting();
  printDirectory(SD.open("/"), 0);
  delay(500);
  isAvailableSD = true;
}

char bluetooth_data;
int old_hex_data = 0;
bool bluetooth_send = false;

//////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////

void loop() {
  if (SerialBT.available()) {
    bluetooth_data = SerialBT.read();
    //  Serial.println(bluetooth_data);
    int hex_data = (int)bluetooth_data;
    Serial.print("hex_data=");
    Serial.println(hex_data);
    if (path[hex_data] != NULL) {
      playing_ = true;
      playbackFile(path[hex_data]);
      bluetooth_send = false;  //bluetooth message send
      old_hex_data = hex_data;
    } else if (bluetooth_data == 'D' && bluetooth_send == false) {
      String son;
      for (int j = 1; j <= file_counter; j++) {
        son = son + String(path[j]) + ',';
      }
      son.replace("/", "");
      Serial.print("Gonderilen=");
      Serial.println(son);
      SerialBT.print(son);
      bluetooth_send = true;  //bluetooth message send
    } else if (bluetooth_data == 'L') {
      while (true) {
        if (bluetooth_data == 'E') {
            Serial.println("donguden çikti1");
            break;  //exit for loop
        }
        if (path[old_hex_data] != NULL) {
          playing_ = true;
          playbackFile(path[old_hex_data]);
          bluetooth_send = false;  //bluetooth message send
        } else if (path[old_hex_data] == NULL) {
          Serial.println("donguden çikti2");
          break;
        } 
        Serial.println("tamamlandi");
      }
    }
  }
}
//////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////
/* Playback a WAV file. Воспроизведение WAV-файла. */
bool playbackFile(char* filePath) {
  uint8_t* dat;
  if (!isAvailableSD) return false;
  // State of process wav file and
  headerState_t state = HEADER_RIFF;
  // Wav file properties
  wavProperties_t wavProps;
  // Open and processing a WAV file
  File file = SD.open(filePath);
  if (file) {
    int n;
    while (file.available()) {
      if (SerialBT.available()) {
        bluetooth_data = SerialBT.read();
        //exit loop
        if (bluetooth_data == 'E') {
          Serial.println("donguden çikti3");
          goto exit;
        }
      }
      switch (state) {
        case HEADER_RIFF:
          wavRiff_t wavRiff;
          n = file.read((uint8_t*)&wavRiff, sizeof(wavRiff_t));
          if (n == sizeof(wavRiff_t)) {
            if (wavRiff.chunkID == CCCC('R', 'I', 'F', 'F') && wavRiff.format == CCCC('W', 'A', 'V', 'E')) {
              state = HEADER_FMT;
              Serial.println("\nHeader RIFF (WAVЕ)");
            } else {
              Serial.println("\nNot .wav file!");
              return false;
            }
          }
          break;
        // properties
        case HEADER_FMT:
          n = file.read((uint8_t*)&wavProps, sizeof(wavProperties_t));
          if (n == sizeof(wavProperties_t)) {
            state = HEADER_DATA;
          }
          break;
        case HEADER_DATA:
          uint32_t chunkId, chunkSize;
          n = read4bytes(file, &chunkId);
          if (n == 4) {
            if (chunkId == CCCC('d', 'a', 't', 'a')) {
              Serial.println("HEADER_DATA");
            }
          }
          n = read4bytes(file, &chunkSize);
          if (n == 4) {
            Serial.println("prepare data");
            state = DATA;
          }
          // Initialize I2S
          dac_i2s_enable();
          i2s_driver_install((i2s_port_t)i2s_num, &i2s_config, 0, NULL);
          // Internal DAC
          i2s_set_pin((i2s_port_t)i2s_num, NULL);
          i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
          // Set sample rate of I2S to sample rate of .wav file
          Serial.print("Sample rate: ");
          i2s_set_sample_rates((i2s_port_t)i2s_num, wavProps.sampleRate);
          Serial.println(wavProps.sampleRate);
          break;
        // process music data
        case DATA:
          uint32_t dataAmp;
          // read data (32 bit)
          n = read4bytes(file, &dataAmp);
          //  data_amp=&dataAmp;
          //  Serial.println(*data_amp/16843009);
          //Serial.println(map(*data_amp,0,0xffff,0,255));
          //  dataAmp=dataAmp*1.5;
          //  delayMicroseconds(25);
          // write data to I2S
          dat = (uint8_t*)&dataAmp;
          // *dat=(*dat)*0.5;
          //  Serial.println(*dat);
          //  Serial.println(*dat);
          i2s_write_bytes((i2s_port_t)i2s_num, dat, sizeof(int32_t), portMAX_DELAY);
          // 32 (bit) =  2 * (number of channels) * 16 (bits per sample)
          //i2s_push_sample((i2s_port_t)i2s_num, (const char*)&dataAmp, 60000);
          break;
      }
    }
exit:
    file.close();
    // Stop and uninstall i2s driver
    i2s_driver_uninstall((i2s_port_t)i2s_num);
    dac_i2s_disable();
    Serial.println("done!!!!!");
    SerialBT.print("ok");  //playing completes
    return true;
  } else {
    Serial.println("Error opening file!");
    return false;
  }
}

/* Read 4 bytes from a file. */
int read4bytes(File file, uint32_t* data) {
  return file.read((uint8_t*)data, sizeof(uint32_t));
}

void dacGreeting(void) {
  Serial.print("Greeting... ");
  unsigned char sample = 0;
  dac_output_enable(DAC_CHANNEL_1);
  dac_output_enable(DAC_CHANNEL_2);
  for (int cnt = 0; cnt < HELLO_LENGTH; cnt++) {
    //  dac_output_voltage(DAC_CHANNEL_1, sample);
    dacWrite(25, (uint8_t)HELLO[cnt]);
    // Sample Rate < 11025
    delayMicroseconds(38);  //22050hz
  }
  dac_output_disable(DAC_CHANNEL_1);
  dac_output_disable(DAC_CHANNEL_2);
  Serial.println("done");
}
/** Получить список файлов директории (dir). */
void printDirectory(File dir, int spNum) {
  // Динамический список
  dyn_list vars;
  constr_list(vars);
  for (uint8_t i = 0; i < spNum; i++) {
    Serial.print("  ");
  }
  Serial.print("> ");
  Serial.println(dir.name());
  // Begin at the start of the directory
  dir.rewindDirectory();
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    // Recurse for directories
    if (entry.isDirectory()) {
      add(vars, entry.name(), true, 0);
      printDirectory(entry, spNum + 1);
    } else {
      // files have sizes, directories do not
      add(vars, entry.name(), false, entry.size());
    }
    entry.close();
  }
  comp* iter = vars.head;

  while (iter != NULL) {
    // files
    if (!iter->is_dir) {
      for (uint8_t i = 0; i < spNum + 1; i++) {
        Serial.print("  ");
      }
      Serial.print("| ");

      if (MusicFile(String(iter->file_name))) {
        file_counter++;
        path[file_counter] = iter->file_name;
      }
      Serial.print(path[file_counter]);
      Serial.print(" (");
      Serial.print(iter->fd_size);
      Serial.println(" bytes)");
    }
    iter = iter->next;
  }
}

bool MusicFile(String FileName) {
  // returns true if file is one of the supported file types, wav
  String ext;
  ext = FileName.substring(FileName.indexOf('.') + 1);
  if ((ext == "WAV") || (ext == "wav"))
    return true;
  else
    return false;
}