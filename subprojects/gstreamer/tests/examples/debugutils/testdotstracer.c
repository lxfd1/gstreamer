#include <gst/gst.h>
#include <gst/video/videooverlay.h>
#include <glib/gstdio.h>  // 文件操作
#include <unistd.h>

// 设置环境变量以启用 DOT 导出
void enable_pipeline_graph_export()
{
  if (g_access("/home/lxf/tmp", F_OK) == -1) {
    // 创建临时目录存放 dot 文件
    g_mkdir_with_parents("/home/lxf/tmp", 0700);
  } 
  // 设置环境变量
  g_setenv("GST_DEBUG_DUMP_DOT_DIR", "/home/lxf/tmp", TRUE);
}

// 生成 pipeline 图像
void generate_pipeline_image(GstBin* pipeline, const gchar* prefix) {
  const gchar* path = g_getenv("GST_DEBUG_DUMP_DOT_DIR");
  gchar* dot_file = g_strdup_printf("%s/%s.dot", path, prefix);
  gchar* png_file = g_strdup_printf("%s/%s.png", path, prefix);

  // 使用调试函数导出 DOT 文件
  gst_debug_bin_to_dot_file(pipeline, GST_DEBUG_GRAPH_SHOW_ALL, prefix);
  g_printerr("dot_file: %s\n", dot_file);

  struct stat st;
  if (stat(dot_file, &st) == 0)
  {
    g_printerr("DOT file size: %lld bytes\n", (long long)st.st_size);
  } else {
    g_printerr("Failed to stat DOT file: %s, error: %s\n", dot_file, g_strerror(errno));
  }

  // 调用 Graphviz 生成图像
  gchar* cmd = g_strdup_printf("dot -Tpng %s -o %s", dot_file, png_file);
  gint ret = system(cmd);

  if (ret == 0) {
    g_print("Pipeline image generated: %s\n", png_file);
  } else {
    g_printerr("Failed to generate pipeline image (%d)\n", ret);
  }

  // 清理
  g_free(dot_file);
  g_free(png_file);
  g_free(cmd);
}

// 总线消息处理器
static gboolean bus_call(GstBus* bus, GstMessage* msg, gpointer data) {
  GMainLoop* loop = (GMainLoop*)data;

  switch (GST_MESSAGE_TYPE(msg)) {
  case GST_MESSAGE_EOS: {
    g_print("End of stream\n");
    g_main_loop_quit(loop);
    break;
  }
  case GST_MESSAGE_ERROR: {
    gchar* debug;
    GError* error;
    gst_message_parse_error(msg, &error, &debug);
    g_printerr("ERROR: %s\n", error->message);
    g_error_free(error);
    g_free(debug);
    g_main_loop_quit(loop);
    break;
  }
  case GST_MESSAGE_STATE_CHANGED: {
    g_print("GST_MESSAGE_STATE_CHANGED\n");
    // 在状态变更时生成图像
    if (GST_MESSAGE_SRC(msg) == GST_OBJECT_CAST(data)) {
      GstState old_state, new_state;
      gst_message_parse_state_changed(msg, &old_state, &new_state, NULL);
      if (new_state == GST_STATE_PLAYING) {
        generate_pipeline_image(GST_BIN(data), "playing_state");
      }
    }
    break;
  }
  default:
    break;
  }
  return TRUE;
}

// 主函数
int main(int argc, char* argv[]) {
  GMainLoop* loop;
  GstElement* pipeline, * src, * filter, * sink;
  GstBus* bus;
  // 启用 DOT 导出
  enable_pipeline_graph_export();
  // 初始化 GStreamer
  gst_init(&argc, &argv);
  loop = g_main_loop_new(NULL, FALSE);
  // 创建 pipeline
  pipeline = gst_pipeline_new("visual-pipeline");
  src = gst_element_factory_make("videotestsrc", "source");
  filter = gst_element_factory_make("capsfilter", "filter");
  sink = gst_element_factory_make("autovideosink", "sink");

  // 设置 filter 属性
  GstCaps* caps = gst_caps_new_simple("video/x-raw",
    "width", G_TYPE_INT, 320,
    "height", G_TYPE_INT, 240,
    NULL);
  g_object_set(G_OBJECT(filter), "caps", caps, NULL);
  gst_caps_unref(caps);

  // 添加元素到 pipeline
  gst_bin_add_many(GST_BIN(pipeline), src, filter, sink, NULL);

  // 连接元素
  if (!gst_element_link_many(src, filter, sink, NULL)) {
    g_printerr("Elements could not be linked\n");
    gst_object_unref(pipeline);
    return -1;
  }

  // 设置总线消息处理器
  bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
  gst_bus_add_watch(bus, bus_call, pipeline);
  gst_object_unref(bus);
  gst_element_set_state(pipeline, GST_STATE_READY);
  GstStateChangeReturn ret = gst_element_get_state(pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  if (ret == GST_STATE_CHANGE_FAILURE)
  {
    g_printerr("Failed to get pipeline state\n");
  } else {
    g_printerr("begin to generate pipeline image\n");
    generate_pipeline_image(GST_BIN(pipeline), "initial_state");
  }

  // 启动 pipeline
  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  g_print("Pipeline running...\n");
  g_main_loop_run(loop);

  // 生成结束状态图像
  generate_pipeline_image(GST_BIN(pipeline), "final_state");

  // 清理资源
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);
  g_main_loop_unref(loop);
  return 0;
}