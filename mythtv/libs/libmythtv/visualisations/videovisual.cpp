#include "mythrender_base.h"
#include "mythplayer.h"
#include "videovisual.h"

VideoVisualFactory* VideoVisualFactory::g_videoVisualFactory = nullptr;

bool VideoVisual::CanVisualise(AudioPlayer *audio, MythRender *render)
{
    if (!audio)
        return false;
    return render && (audio->GetNumChannels() == 2 || audio->GetNumChannels() == 1);
}

QStringList VideoVisual::GetVisualiserList(RenderType type)
{
    QStringList result;
    VideoVisualFactory* factory;
    for (factory = VideoVisualFactory::VideoVisualFactories();
         factory; factory = factory->next())
    {
        if (factory->SupportedRenderer(type))
            result << factory->name();
    }
    result.sort();
    return result;
}

VideoVisual* VideoVisual::Create(const QString &name,
                                 AudioPlayer *audio, MythRender *render)
{
    if (!audio || !render || name.isEmpty())
        return nullptr;

    const VideoVisualFactory* factory;
    for (factory = VideoVisualFactory::VideoVisualFactories();
         factory; factory = factory->next())
    {
        if (name.isEmpty())
            return factory->Create(audio, render);
        if (factory->name() == name)
            return factory->Create(audio, render);
    }
    return nullptr;
}

VideoVisual::VideoVisual(AudioPlayer *audio, MythRender *render)
  : m_audio(audio), m_render(render)
{
    m_lastUpdate = MythDate::current();
    mutex()->lock();
    if (m_audio)
        m_audio->addVisual(this);
    mutex()->unlock();
}

VideoVisual::~VideoVisual()
{
    mutex()->lock();
    if (m_audio)
        m_audio->removeVisual(this);
    DeleteNodes();
    mutex()->unlock();
}

int64_t VideoVisual::SetLastUpdate(void)
{
    QDateTime now = MythDate::current();
    int64_t result = m_lastUpdate.time().msecsTo(now.time());
    m_lastUpdate = now;
    return result;
}

// caller holds lock
void VideoVisual::DeleteNodes(void)
{
    while (!m_nodes.empty())
    {
        delete m_nodes.back();
        m_nodes.pop_back();
    }
}

// caller holds lock
void VideoVisual::prepare()
{
    DeleteNodes();
}

// caller holds lock
VisualNode* VideoVisual::GetNode(void)
{
    int64_t timestamp = m_audio->GetAudioTime();
    while (m_nodes.size() > 1)
    {
        if (m_nodes.front()->m_offset > timestamp)
            break;
        delete m_nodes.front();
        m_nodes.pop_front();
    }

    if (m_nodes.isEmpty())
        return nullptr;

    return m_nodes.first();
}

// TODO Add MMX path
static inline void stereo16_from_stereofloat32(
    short l[], short r[], const float s[], unsigned long cnt)
{
    const float f((1 << 15) - 1);
    while (cnt--)
    {
        *l++ = short(f * *s++);
        *r++ = short(f * *s++);
    }
}

// TODO Add MMX path
static inline void mono16_from_monofloat32(
    short l[], const float s[], unsigned long cnt)
{
    const float f((1 << 15) - 1);
    while (cnt--)
        *l++ = short(f * *s++);
}

// caller holds lock
void VideoVisual::add(const void *b, unsigned long b_len, unsigned long w, int c,
                      int p)
{
    if (!m_disabled && m_nodes.size() > 500)
    {
        LOG(VB_GENERAL, LOG_ERR, DESC +
            QString("Over 500 nodes buffered - disabling visualiser."));
        DeleteNodes();
        m_disabled = true;
    }

    if (m_disabled)
        return;

    long len = b_len, cnt;
    short *l = nullptr, *r = nullptr;

    len /= c;
    len /= (p / 8);

    if (len > 512)
        len = 512;

    cnt = len;

    if (c == 2)
    {
        l = new short[len];
        r = new short[len];

        if (p == 8)
            stereo16_from_stereopcm8(l, r, (uchar *) b, cnt);
        else if (p == 16)
            stereo16_from_stereopcm16(l, r, (short *) b, cnt);
        else if (p == 32)
            stereo16_from_stereofloat32(l, r, reinterpret_cast<const float * >(b), cnt);
        else
            len = 0;
    }
    else if (c == 1)
    {
        l = new short[len];

        if (p == 8)
            mono16_from_monopcm8(l, (uchar *) b, cnt);
        else if (p == 16)
            mono16_from_monopcm16(l, (short *) b, cnt);
        else if (p == 32)
            mono16_from_monofloat32(l, reinterpret_cast<const float * >(b), cnt);
        else
            len = 0;
    }
    else
        len = 0;

    m_nodes.append(new VisualNode(l, r, len, w));
}
