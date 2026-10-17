#pragma once
// Samples may live in a ring: sample k is values[(start+k)%capacity].
static inline uint8_t rawByte(const uint16_t *values,uint32_t capacity,uint32_t start,size_t j) {
  uint32_t index=start+uint32_t(j/2);if(index>=capacity)index-=capacity;
  const uint16_t sample=values[index];return j&1?sample>>8:sample&255;
}
static void opticalTransferRaw(const uint16_t *values,uint32_t capacity,uint64_t first,uint32_t count,int64_t elapsed,
    uint32_t drops,uint32_t errors,bool complete,esp_err_t err,bool displayBlanking=true) {
  const uint32_t start=uint32_t(first%capacity);size_t size=size_t(count)*2,sent=0;
  uint32_t checksum=2166136261u;
  for(size_t i=0;i<size;i++){checksum^=rawByte(values,capacity,start,i);checksum*=16777619u;}
  Serial.printf("RAW_BEGIN {\"samples\":%lu,\"bytes\":%lu,\"requested_sps\":%lu,\"elapsed_us\":%lld,\"overflow_events\":%lu,\"read_errors\":%lu,\"complete\":%s,\"fnv1a32\":%lu,\"adc_error\":%d,\"display_blanking\":%s,\"display_marker_resolution_us\":5120}\n",
    (unsigned long)count,(unsigned long)size,(unsigned long)ANALOG_SAMPLE_HZ,(long long)elapsed,
    (unsigned long)drops,(unsigned long)errors,complete?"true":"false",(unsigned long)checksum,(int)err,displayBlanking?"true":"false");
  // ACKed, independently checksummed hexadecimal blocks tolerate USB loss.
  Serial.setTxTimeoutMs(1000);bool transferOk=true;
  char packet[1200];const char *hex="0123456789abcdef";
  while(sent<size && transferOk) {
    size_t n=size-sent;if(n>512)n=512;
    uint32_t blockHash=2166136261u;
    uint8_t data[512];for(size_t j=0;j<n;j++)data[j]=rawByte(values,capacity,start,sent+j);
    for(size_t j=0;j<n;j++){blockHash^=data[j];blockHash*=16777619u;}
    int prefix=snprintf(packet,sizeof(packet),"RAW_CHUNK %lu %lu %lu ",(unsigned long)sent,(unsigned long)n,(unsigned long)blockHash);
    for(size_t j=0;j<n;j++){packet[prefix+j*2]=hex[data[j]>>4];packet[prefix+j*2+1]=hex[data[j]&15];}
    size_t length=prefix+n*2;packet[length++]='\n';
    bool ack=false;
    for(unsigned attempt=0;attempt<5 && !ack;attempt++) {
      Serial.println();Serial.write((uint8_t*)packet,length);Serial.flush();
      char reply[40];unsigned used=0;int64_t waitUntil=esp_timer_get_time()+2000000;
      while(esp_timer_get_time()<waitUntil && !ack) {
        while(Serial.available()) {
          char c=Serial.read();
          if(c=='\n') {reply[used]=0;unsigned long offset=0;if(sscanf(reply,"ACK %lu",&offset)==1 && offset==sent)ack=true;used=0;}
          else if(c!='\r' && used<sizeof(reply)-1)reply[used++]=c;
        }
        delay(1);
      }
    }
    if(ack)sent+=n;else transferOk=false;
  }
  Serial.println(transferOk?"RAW_END":"RAW_ERROR transfer_ack_timeout");Serial.setTxTimeoutMs(20);
}
