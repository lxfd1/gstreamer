#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <gst/rtp/rtp.h>
// #include <gst/rtpmanager/gstrtpbin.h>
#include <gst/rtp/gstrtcpbuffer.h>

#define DEFAULT_RTSP_PORT "8554"

// RTCP回调函数，用于处理接收到的RTCP包

static void
on_session_receiving_rtcp(GObject * session, GstBuffer *buffer, gpointer user_data)
{
    g_print(">> Received RTCP Packet ===\n");
    GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
    if (!gst_rtcp_buffer_validate(buffer)) {
        g_print("Invalid RTCP packet\n");
        return;
    }
    gst_rtcp_buffer_map(buffer, GST_MAP_READ, &rtcp);
    GstRTCPPacket packet;
    gst_rtcp_buffer_get_first_packet(&rtcp, &packet);

    do {

        switch (gst_rtcp_packet_get_type(&packet)) {
            case GST_RTCP_TYPE_SR: {
                guint ssrc;
                guint64 ntptime;
                guint32 rtptime;
                guint32 packet_count, octet_count;
                gst_rtcp_packet_sr_get_sender_info(&packet, &ssrc, &ntptime, &rtptime,
                                     &packet_count, &octet_count);
                g_print("Sender Report (SSRC: %u)\n", ssrc);
                g_print("NTP Time: %lu, RTP Time: %u\n", ntptime, rtptime);
                g_print("Packets: %u, Bytes: %u\n\n", packet_count, octet_count);
                break;
            }    
            case GST_RTCP_TYPE_RR: {
                guint ssrc;
                guint8 fraction;
                guint32 lost;
                guint32 last_seq;
                guint32 jitter;
                guint32 lsr;
                guint32 dlsr;

                int count = gst_rtcp_packet_get_rb_count(&packet);
                for (int i = 0; i < count; i++)
                {
                    guint32 ssrc, exthighestseq, jitter, lsr, dlsr;
                    guint8 fractionlost;
                    gint32 packetslost;

                    gst_rtcp_packet_get_rb(&packet, i, &ssrc, &fractionlost,
                                           &packetslost, &exthighestseq, &jitter, &lsr, &dlsr);

                    g_print("RB %d from SSRC %u, jitter %u\n", i,
                            ssrc, jitter);
                }    
                break;
            }
            
            default:
                break;
        }
    } while (gst_rtcp_packet_move_to_next(&packet));
    gst_rtcp_buffer_unmap(&rtcp);
    g_print("<< Received RTCP Packet ===\n");
}


static void on_new_ssrc_cb(GstElement *rtpbin, guint session, guint32 ssrc, gpointer user_data)
{
    g_print("on_new_ssrc_cb session %u, ssrc:%u, rtpbin:%p\n", session, ssrc, rtpbin);
    GObject *rtpsession;
    g_signal_emit_by_name(rtpbin, "get-internal-session", session, &rtpsession);
    g_print("on_new_ssrc_cb, g_signal_emit_by_name:%p\n", rtpsession);
    if (rtpsession)
    {
        g_signal_connect_object(rtpsession, "on-receiving-rtcp", G_CALLBACK(on_session_receiving_rtcp), NULL, 0);
        g_print("Connected on-receiving-rtcp signal for session %u\n", session);
        gst_object_unref(rtpsession);
    }
}

// 在 rtpbin 被添加到 pipeline 时立即注册回调
static void on_rtpbin_added(GstBin *bin, GstElement *element, gpointer user_data) {
    g_print("on_element_added, name=%s\n", gst_element_get_name(element));
    if (g_strcmp0(gst_element_get_name(element), "rtpbin0") == 0)
    {
        g_signal_connect(element, "on_new_ssrc", (GCallback)on_new_ssrc_cb, NULL);
        g_signal_handlers_disconnect_by_func(bin, G_CALLBACK(on_rtpbin_added), user_data);
    }
}

static void media_configure_callback(GstRTSPMediaFactory *factory, GstRTSPMedia *media, gpointer user_data)
{
    /* connect our prepared signal so that we can see when this media is
     * prepared for streaming */
    GstElement *element = gst_rtsp_media_get_element(media);
    if(!element) {
        g_print("Failed to get pipeline from media\n");
        return;
    }

    // 获取element的父元素，这个父元素应该是pipeline
    GstElement *parent = GST_ELEMENT(gst_object_get_parent(GST_OBJECT(element)));
    if (!parent) {
        g_print("Element has no parent\n");
        gst_object_unref(element);
        return;
    }

    // 现在，我们有了pipeline（parent），我们可以通过名称来验证
    if (g_strcmp0(GST_OBJECT_NAME(parent), "media-pipeline") == 0) {
        g_print("Found the pipeline by name: media-pipeline\n");
        // 现在parent就是我们要的pipeline
        // 对pipeline进行操作...
    } else {
        g_print("Parent is not the expected pipeline: %s\n", GST_OBJECT_NAME(parent));
        gst_object_unref(element);
        return;
    }

    // GstRTSPMediaPrivate *priv = gst_rtsp_media_get_instance_private(media);
    // GstElement *pipeline = priv->pipeline; // 注意：这里没有增加引用计数，调用者需要增加引用
    // 监听 rtpbin 被添加到 pipeline 的事件
    g_signal_connect(GST_BIN(parent), "element-added", G_CALLBACK(on_rtpbin_added), NULL);
    g_print("media_configure_callback, pipeline:%p, element:%p\n", parent, element);
    gst_object_unref(element);
}

int main(int argc, char *argv[])
{
    GMainLoop *loop;
    GstRTSPServer *server;
    GstRTSPMountPoints *mounts;
    GstRTSPMediaFactory *factory;
    
    gst_init(&argc, &argv);
    loop = g_main_loop_new(NULL, FALSE);
    
    // 创建RTSP服务器
    server = gst_rtsp_server_new();
    g_object_set(server, "service", DEFAULT_RTSP_PORT, NULL);
    mounts = gst_rtsp_server_get_mount_points(server);
    factory = gst_rtsp_media_factory_new();
    // 配置媒体工厂
    gst_rtsp_media_factory_set_launch (factory,
      "( rtpbin name=rtpbin videotestsrc is-live=1 ! x264enc ! rtph264pay name=pay0 pt=96 )");

    // 设置RTCP监控
    g_signal_connect(factory, "media-configure", G_CALLBACK(media_configure_callback), NULL);
   
    gst_rtsp_mount_points_add_factory(mounts, "/test", factory);
    g_object_unref(mounts);

    // 启动服务器
    gst_rtsp_server_attach(server, NULL);
    g_print("RTSP Server running at rtsp://127.0.0.1:%s/test\n", DEFAULT_RTSP_PORT);
    g_main_loop_run(loop);
    
    return 0;
}