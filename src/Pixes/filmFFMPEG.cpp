////////////////////////////////////////////////////////
//
// GEM - Graphics Environment for Multimedia
//
// zmoelnig@iem.kug.ac.at
//
// Implementation file 
//
//    Copyright (c) 1997-1999 Mark Danks.
//    Copyright (c) G�nther Geiger.
//    Copyright (c) 2001-2002 IOhannes m zmoelnig. forum::f�r::uml�ute. IEM
//    For information on usage and redistribution, and for a DISCLAIMER OF ALL
//    WARRANTIES, see the file, "GEM.LICENSE.TERMS" in this distribution.
//
/////////////////////////////////////////////////////////
#include "Pixes/filmFFMPEG.h"

/////////////////////////////////////////////////////////
//
// filmFFMPEG
//
/////////////////////////////////////////////////////////
// Constructor
//
/////////////////////////////////////////////////////////

filmFFMPEG :: filmFFMPEG(int format) : film(format) {
  static bool first_time=true;
  if (first_time) {
#ifdef HAVE_FFMPEG
    av_register_all();
    post("pix_film:: FFMPEG support");
#endif
    first_time = false;
  }
#ifdef HAVE_FFMPEG
  m_Format=NULL;
#endif
}

/////////////////////////////////////////////////////////
// Destructor
//
/////////////////////////////////////////////////////////
filmFFMPEG :: ~filmFFMPEG()
{
  close();
}
#ifdef HAVE_FFMPEG
void filmFFMPEG :: close(void)
{
  if (m_Format){
    if(m_curTrack>=0 && 
       NULL!=m_Format->streams[m_curTrack] && 
       NULL!=&m_Format->streams[m_curTrack]->codec){
      avcodec_close(&m_Format->streams[m_curTrack]->codec);
    }
    av_close_input_file(m_Format);
    m_Format=0;
  }
}

/////////////////////////////////////////////////////////
// really open the file ! (OS dependent)
//
/////////////////////////////////////////////////////////
bool filmFFMPEG :: open(char *filename, int format)
{
  int i=0;
  int state=0;
  AVCodec* codec=0;

  int err = av_open_input_file(&m_Format,filename,NULL,0,NULL);
  if (err < 0) {
    goto unsupported;
  }

  if (format>0)m_wantedFormat=format;

  state=1;
  err = av_find_stream_info(m_Format);
  if (err < 0) {
    goto unsupported;
  }
  m_numTracks = m_Format->nb_streams;
  for (i=0;i<m_Format->nb_streams;i++) { 
    codec = avcodec_find_decoder(m_Format->streams[i]->codec.codec_id);
    if (!codec) continue;
    if (m_Format->streams[i]->codec.codec_type == CODEC_TYPE_VIDEO)
      break;
  }

  m_curTrack = i;  // remember the stream
  state=2;
  //  if (i == m_Format->nb_streams) { /* jmz: ?? weird error-check ??
  if(i < 0 || NULL==codec) {
    goto unsupported;
  }

  //  post("codec=%x", codec); post("streamcodec = %x %X", &m_Format->streams[i]->codec, m_Format->streams[i]->codec);
  state=3;
  err = avcodec_open(&m_Format->streams[i]->codec,codec);

  if (err < 0) {
    goto unsupported;
  }
  m_curFrame = 0;
  m_wantedFrame=0;
  m_wantedTrack=m_curTrack;

  // get all of the information about the stream
  
  // Get the length of the movie
  //AVStream*stream=m_Format->streams[i];
  if(0!=(int)m_Format->streams[i]->duration){
    int frames=(int)((((t_float)(m_Format->streams[i]->duration))/AV_TIME_BASE)*
                (m_Format->streams[i]->codec.frame_rate));
    m_numFrames=frames;
    //    post("%d :: %d %d (%d) %d", m_numFrames, (int)m_Format->streams[i]->duration,(int)m_Format->duration, (int)AV_TIME_BASE,(int)m_Format->streams[i]->codec.frame_rate);

  } else
    m_numFrames = -1;

  m_readNext=true;

#if FFMPEG_VERSION_INT >= 0x000409
  m_allowSeek=true;
  if(av_seek_frame(m_Format, 0, 0, 0))
    m_allowSeek=false;
#endif /* FFMPEG_VERSION */

  m_image.image.xsize = m_Format->streams[i]->codec.width;
  m_image.image.ysize = m_Format->streams[i]->codec.height;
  m_image.image.csize=4;
  m_image.image.format=GL_RGBA;
  m_image.image.reallocate();

  m_PacketLen = 0;
  m_PacketPtr = NULL;
  m_Pkt.data = NULL;
  return true;

 unsupported:
  startpost("FFMPEG failed");
  switch (state){
  case(0):
    startpost(" at opening");
    break;
  case(1):
    startpost(" at stream");
    break;
  case(2):
    startpost(" at codec");
    break;
  case(3):
    startpost(" at CoDec");
    break;
  default:;
  }
  switch(err){
  case(AVERROR_UNKNOWN):startpost(" [unknown error]"); break;
  case(AVERROR_IO):startpost(" [i/o error]"); break;
  case(AVERROR_NUMEXPECTED): startpost(" [number syntax expected in filename]"); break;
  case(AVERROR_INVALIDDATA): startpost(" [invalid data found]"); break;
  case(AVERROR_NOMEM):startpost(" [not enough memory]"); break;
  case(AVERROR_NOFMT):startpost(" [unknown format]"); break;
  case(AVERROR_NOTSUPP):startpost(" [operation not supported]"); break;
  default:startpost(" [%d]", err);
     }
  close();
  return false;
}

/////////////////////////////////////////////////////////
// render
//
/////////////////////////////////////////////////////////
pixBlock* filmFFMPEG :: getFrame(){
  uint8_t* ptr;
  int len;
  int i;
  int gotit = 0;
  int ret;
  if (m_Format) {
    if (!m_readNext)return &m_image;
#if FFMPEG_VERSION_INT >= 0x000409
    if(m_allowSeek && (m_wantedTrack!=m_curTrack || m_wantedFrame!=m_curFrame)){
      int err=0;
      int64_t timestamp = m_wantedFrame;
      int flags = AVSEEK_FLAG_ANY;
      if(m_wantedFrame<m_curFrame)flags|=AVSEEK_FLAG_BACKWARD;
      err = av_seek_frame(m_Format, m_wantedTrack, timestamp, flags);
      if(-1==err)m_allowSeek=false;
      m_curTrack=m_wantedTrack;
    }
#endif
    len = m_PacketLen;
    ptr = m_PacketPtr;
    while (!gotit) {
      if (len == 0) { 
	if (av_read_packet(m_Format,&m_Pkt) < 0) {
	  // ?? TODO is this the only way to say goodbye 
	  m_numFrames = m_Format->streams[m_curTrack]->codec.frame_number;
	  break;
	}
	ptr = m_Pkt.data;
	len = m_Pkt.size;
      }
#if LIBAVCODEC_VERSION_INT >= 0x000406
      ret = avcodec_decode_video(&m_Format->streams[m_curTrack]->codec,
				 &m_avFrame,
				 &gotit,
				 ptr,
				 len);
      for(int i=0;i<4;i++){
	m_Picture.data[i]=m_avFrame.data[i];
	m_Picture.linesize[i]=m_avFrame.linesize[i];
      }
#else
      ret = avcodec_decode_video(&m_Format->streams[m_curTrack]->codec,
				 &m_Picture,
				 &gotit,
				 ptr,
				 len);
#endif
      if (ret < 0 ) { // TODO recover gracefully 
	post("error while decoding");
	break;
      }
      len-=ret;
      ptr+=ret;
      if (!len) av_free_packet(&m_Pkt);
      
      if (gotit) {
	AVPicture rgba;
	int dstfmt=0; 
	m_image.image.format=m_wantedFormat;
	m_image.image.upsidedown=true;
	switch(m_wantedFormat){
	case GL_LUMINANCE:     dstfmt = PIX_FMT_GRAY8;  break;
	case GL_YCBCR_422_GEM: dstfmt = PIX_FMT_YUV422; break;
	default:
	case GL_RGBA:          dstfmt = PIX_FMT_RGBA32; break;
	}
	m_image.image.setCsizeByFormat(m_wantedFormat);
	m_image.image.reallocate();
	int width = m_Format->streams[m_curTrack]->codec.width;
	int height = m_Format->streams[m_curTrack]->codec.height;
	int fmt = m_Format->streams[m_curTrack]->codec.pix_fmt;
	avpicture_fill(&rgba,m_image.image.data,dstfmt,width,height);
	// cannot convert yuv420 to yuv422
	if (img_convert(&rgba,dstfmt,&m_Picture,fmt,width,height)<0)
	  post("pix_film: image conversion failed (%d->%d)", fmt, dstfmt);
	if(m_wantedFormat==GL_RGBA){
	  m_image.image.swapRedBlue();
	}	
	m_curFrame = m_Format->streams[m_curTrack]->codec.frame_number;
      }
    }
    m_PacketLen = len;
    m_PacketPtr = ptr;
    m_image.newimage=1;
    m_readNext = false;
    return &m_image;
  }
  return 0;
}

int filmFFMPEG :: changeImage(int imgNum, int trackNum){
  if(m_Format){
    m_wantedFrame=(imgNum==-1)?m_curFrame:imgNum;
    m_wantedTrack=(trackNum==-1)?m_curTrack:trackNum;
    m_readNext = true;
    if(m_numFrames!=-1){
      if(m_wantedFrame<0 || m_wantedFrame>=m_numFrames)
        return FILM_ERROR_FAILURE;
    }
  }
  return FILM_ERROR_DONTKNOW;
}
#endif /* HAVE_FFMPEG */
