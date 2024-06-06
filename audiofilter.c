#include <gst/gst.h>

#define GST_TYPE_MY_VOLUME_FILTER (my_volume_filter_get_type())
G_DECLARE_FINAL_TYPE(MyVolumeFilter, my_volume_filter, MY, VOLUME_FILTER, GstElement)

struct _MyVolumeFilter {
    GstElement parent;
    gdouble volume;
};

G_DEFINE_TYPE(MyVolumeFilter, my_volume_filter, GST_TYPE_ELEMENT)

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("audio/x-raw, format=(string)S16LE, layout=(string)interleaved, channels=(int)[1, 2]")
);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("audio/x-raw, format=(string)S16LE, layout=(string)interleaved, channels=(int)[1, 2]")
);

enum {
    PROP_0,
    PROP_VOLUME,
    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

static void my_volume_filter_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    MyVolumeFilter *filter = GST_MY_VOLUME_FILTER(object);

    switch (prop_id) {
        case PROP_VOLUME:
            filter->volume = g_value_get_double(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void my_volume_filter_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    MyVolumeFilter *filter = GST_MY_VOLUME_FILTER(object);

    switch (prop_id) {
        case PROP_VOLUME:
            g_value_set_double(value, filter->volume);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void my_volume_filter_class_init(MyVolumeFilterClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gobject_class->set_property = my_volume_filter_set_property;
    gobject_class->get_property = my_volume_filter_get_property;

    obj_properties[PROP_VOLUME] = g_param_spec_double(
        "volume",
        "Volume",
        "Volume level (0.0 to 10.0)",
        0.0, 10.0, 1.0,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
    );

    g_object_class_install_properties(gobject_class, N_PROPERTIES, obj_properties);

    gst_element_class_set_static_metadata(
        element_class,
        "Volume Filter",
        "Filter/Audio",
        "Adjusts the volume of audio data",
        "Author"
    );

    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_add_static_pad_template(element_class, &src_template);
}

static void my_volume_filter_init(MyVolumeFilter *filter) {
    filter->volume = 1.0;
}

static GstFlowReturn my_volume_filter_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
    MyVolumeFilter *filter = GST_MY_VOLUME_FILTER(trans);
    GstMapInfo map;

    gst_buffer_map(buf, &map, GST_MAP_READWRITE);

    gint16 *data = (gint16 *)map.data;
    guint len = map.size / sizeof(gint16);

    for (guint i = 0; i < len; i++) {
        data[i] = (gint16)(data[i] * filter->volume);
    }

    gst_buffer_unmap(buf, &map);

    return GST_FLOW_OK;
}

static gboolean my_volume_filter_plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "my-volumefilter", GST_RANK_NONE, GST_TYPE_MY_VOLUME_FILTER);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    my-volumefilter,
    "Audio Volume Filter",
    my_volume_filter_plugin_init,
    "1.0",
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)