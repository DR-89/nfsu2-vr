package org.nfsu2.quest;

import java.nio.charset.StandardCharsets;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.util.ArrayDeque;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;
import okhttp3.WebSocket;
import okhttp3.WebSocketListener;
import okio.ByteString;

/** TLS is verified by Android's default trust manager and hostname verifier.
 * No credentials in URLs, no redirects, no permissive trust or cleartext to the
 * public service. Only a network worker polls this bridge, never the VR thread. */
public final class XyzWebSocket {
    private static final int MAX_MESSAGE = 1024 * 1024, MAX_BINARY = 65536, MAX_QUEUE = 4 * 1024 * 1024;
    private static final OkHttpClient CLIENT = new OkHttpClient.Builder()
        .connectTimeout(8, TimeUnit.SECONDS).writeTimeout(8, TimeUnit.SECONDS)
        .pingInterval(15, TimeUnit.SECONDS).followRedirects(false).followSslRedirects(false)
        .retryOnConnectionFailure(false).build();
    private static final ScheduledExecutorService CLOSER = Executors.newSingleThreadScheduledExecutor(r -> {
        Thread t = new Thread(r, "xyz-close"); t.setDaemon(true); return t;
    });
    private final OkHttpClient client;
    private final String origin;
    private final ArrayDeque<byte[]> events = new ArrayDeque<>();
    private WebSocket socket;
    private long generation;
    private int queuedBytes;
    private boolean ended;

    public XyzWebSocket() { this(CLIENT, "https://nfsu2.xyz"); }
    // Package-private test seam; never exposed through JNI or a player setting.
    XyzWebSocket(OkHttpClient client, String origin) {
        this.client = client; this.origin = origin;
        if (!origin.equals("https://nfsu2.xyz") && !origin.matches("http://127\\.0\\.0\\.1:[0-9]+"))
            throw new IllegalArgumentException("Only the service or loopback tests are allowed");
    }
    /** Read-only name permission check on the native network worker. The
     * user's keyphrase is never sent; its derived token stays in the header. */
    public byte[] nameInfo(String name, String claim) {
        if (name == null || claim == null || !name.matches("[A-Za-z0-9_-]{3,15}") ||
            (!claim.isEmpty() && !claim.matches("[A-Za-z0-9_-]{43}"))) return null;
        Request.Builder request = new Request.Builder().url(origin + "/names?name=" + name)
            .header("User-Agent", "NFSU2-VR/0.6 native").header("Cache-Control", "no-store")
            .header("Accept", "application/json");
        if (!claim.isEmpty()) request.header("Authorization", "Bearer " + claim);
        OkHttpClient http = client.newBuilder().callTimeout(8, TimeUnit.SECONDS)
            .readTimeout(8, TimeUnit.SECONDS).followRedirects(false).followSslRedirects(false)
            .retryOnConnectionFailure(false).build();
        try (Response response = http.newCall(request.build()).execute()) {
            if (response.code() != 200 || response.body() == null || response.body().contentLength() > 8192) return null;
            try (InputStream in = response.body().byteStream()) {
                ByteArrayOutputStream out = new ByteArrayOutputStream();byte[] bytes = new byte[1024];
                for (int n; (n = in.read(bytes)) != -1;) {
                    if (out.size() + n > 8192) return null;
                    out.write(bytes, 0, n);
                }
                return out.toByteArray();
            }
        } catch (Exception ignored) { return null; }
    }
    private synchronized boolean event(long epoch, int kind, int code, byte[] data) {
        if (epoch != generation || ended) return false;
        if (data.length > (kind == 1 ? MAX_MESSAGE : MAX_BINARY) || events.size() >= 256 || queuedBytes + data.length + 4 > MAX_QUEUE) {
            events.clear(); queuedBytes = 0; kind = 3; code = 1009; data = new byte[0];
        }
        byte[] e = new byte[data.length + 4]; e[0] = (byte)kind;
        e[1] = (byte)(code >> 8); e[2] = (byte)code;
        System.arraycopy(data, 0, e, 4, data.length);
        events.add(e); queuedBytes += e.length;
        if (kind == 3) ended = true;
        return !ended;
    }
    public synchronized boolean connect(String path) {
        if (!path.equals("/lan?queue") && !path.equals("/lan?queue&resume")) return false;
        close(); events.clear(); queuedBytes = 0; ended = false;
        final long epoch = generation;
        Request request = new Request.Builder().url(origin + path).header("User-Agent", "NFSU2-VR/0.6 native").build();
        socket = client.newWebSocket(request, new WebSocketListener() {
            @Override public void onOpen(WebSocket ws, Response response) { if (!event(epoch, 0, 0, new byte[0])) ws.cancel(); }
            @Override public void onMessage(WebSocket ws, String text) {
                if (!event(epoch, 1, 0, text.getBytes(StandardCharsets.UTF_8))) ws.cancel();
            }
            @Override public void onMessage(WebSocket ws, ByteString bytes) {
                if (!event(epoch, 2, 0, bytes.toByteArray())) ws.cancel();
            }
            @Override public void onClosing(WebSocket ws, int code, String reason) {
                event(epoch, 3, code, new byte[0]); ws.close(code, null);
            }
            @Override public void onClosed(WebSocket ws, int code, String reason) { event(epoch, 3, code, new byte[0]); }
            @Override public void onFailure(WebSocket ws, Throwable failure, Response response) {
                event(epoch, 3, 1006, new byte[0]); // No payload/URL/credential logging.
            }
        });
        return true;
    }
    public synchronized boolean send(byte[] data, boolean text) {
        if (socket == null || ended || data == null || data.length == 0 || data.length > (text ? MAX_MESSAGE : MAX_BINARY) || socket.queueSize() + data.length > MAX_QUEUE) return false;
        return text ? socket.send(new String(data, StandardCharsets.UTF_8)) : socket.send(ByteString.of(data));
    }
    public synchronized byte[] poll() {
        byte[] event = events.poll(); if (event != null) queuedBytes -= event.length; return event;
    }
    public synchronized void close() {
        ++generation; ended = true;
        if (socket != null) {
            WebSocket old = socket; socket = null;
            old.close(1000, "idle"); CLOSER.schedule(old::cancel, 2, TimeUnit.SECONDS);
        }
        events.clear(); queuedBytes = 0;
    }
}
