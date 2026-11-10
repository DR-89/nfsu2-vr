package org.nfsu2.quest;

import static org.junit.Assert.*;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.TimeUnit;
import okhttp3.OkHttpClient;
import okhttp3.Response;
import okhttp3.WebSocket;
import okhttp3.WebSocketListener;
import okhttp3.mockwebserver.MockResponse;
import okhttp3.mockwebserver.MockWebServer;
import okio.ByteString;
import org.junit.Test;

public class XyzWebSocketTest {
    @Test public void namesUseOnlyBoundedVerifiedResponseAndHeader() throws Exception {
        try (MockWebServer server = new MockWebServer()) {
            XyzWebSocket socket = client(server);
            assertNull(socket.nameInfo("bad/name", ""));
            assertNull(socket.nameInfo("Test_VR", "bad\r\nheader"));
            server.enqueue(new MockResponse().setBody("{\"name\":\"Test_VR\",\"state\":\"yours\"}"));
            char[] token = new char[43];java.util.Arrays.fill(token, 'a');
            assertNotNull(socket.nameInfo("Test_VR", new String(token)));
            okhttp3.mockwebserver.RecordedRequest request = server.takeRequest();
            assertEquals("/names?name=Test_VR", request.getPath());
            assertEquals("Bearer " + new String(token), request.getHeader("Authorization"));
            assertEquals("no-store", request.getHeader("Cache-Control"));
            server.enqueue(new MockResponse().setResponseCode(302).addHeader("Location", "/unexpected"));
            assertNull(socket.nameInfo("Test_VR", ""));
            assertNull(server.takeRequest().getHeader("Authorization"));
            char[] large = new char[8193];java.util.Arrays.fill(large, 'L');
            server.enqueue(new MockResponse().setChunkedBody(new String(large), 1000));
            assertNull(socket.nameInfo("Test_VR", ""));
            server.takeRequest();assertEquals(3, server.getRequestCount());
        }
    }
    private byte[] next(XyzWebSocket socket) throws Exception {
        long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while (System.nanoTime() < end) {
            byte[] e = socket.poll(); if (e != null) return e;
            Thread.sleep(5);
        }
        fail("Timed out waiting for WebSocket event"); return null;
    }
    private XyzWebSocket client(MockWebServer server) {
        return new XyzWebSocket(new OkHttpClient.Builder().followRedirects(false).followSslRedirects(false).build(),
            "http://127.0.0.1:" + server.getPort());
    }
    @Test public void framesCloseAndReconnectGeneration() throws Exception {
        try (MockWebServer server = new MockWebServer()) {
            server.enqueue(new MockResponse().withWebSocketUpgrade(new WebSocketListener() {
                @Override public void onMessage(WebSocket ws, String text) { ws.send(text); }
                @Override public void onMessage(WebSocket ws, ByteString bytes) { ws.send(bytes); ws.close(4003, "test"); }
            }));
            XyzWebSocket socket = client(server);
            try {
                assertFalse(socket.connect("/lan?claim=secret"));
                assertTrue(socket.connect("/lan?queue"));
                assertEquals(0, next(socket)[0]);
                byte[] hello = "{\"t\":\"hello\"}".getBytes(StandardCharsets.UTF_8);
                assertTrue(socket.send(hello, true));
                byte[] e = next(socket); assertEquals(1, e[0]);
                assertArrayEquals(hello, java.util.Arrays.copyOfRange(e, 4, e.length));
                byte[] data = {0, 1, (byte)0xff}; assertTrue(socket.send(data, false));
                e = next(socket); assertEquals(2, e[0]); assertArrayEquals(data, java.util.Arrays.copyOfRange(e, 4, e.length));
                e = next(socket); assertEquals(3, e[0]); assertEquals(4003, (e[1] & 255) * 256 + (e[2] & 255));
                assertFalse(socket.send(data, false));
                socket.close(); Thread.sleep(30); assertNull(socket.poll());
                assertEquals("/lan?queue", server.takeRequest().getPath());
                server.enqueue(new MockResponse().withWebSocketUpgrade(new WebSocketListener() {
                    @Override public void onClosing(WebSocket ws, int code, String reason) { ws.close(code, reason); }
                }));
                assertTrue(socket.connect("/lan?queue&resume")); assertEquals(0, next(socket)[0]);
                assertNull(socket.poll());
            } finally { socket.close(); }
        }
    }
    @Test public void rejectsOversizedMessages() throws Exception {
        try (MockWebServer server = new MockWebServer()) {
            server.enqueue(new MockResponse().withWebSocketUpgrade(new WebSocketListener() {
                @Override public void onOpen(WebSocket ws, Response r) { ws.send(ByteString.of(new byte[65537])); }
            }));
            XyzWebSocket socket = client(server);
            try {
                assertTrue(socket.connect("/lan?queue"));
                byte[] e = next(socket); if (e[0] == 0) e = next(socket);
                assertEquals(3, e[0]); assertEquals(1009, (e[1] & 255) * 256 + (e[2] & 255));
                assertFalse(socket.send(new byte[65537], false));
            } finally { socket.close(); }
        }
    }
    @Test public void acceptsLargeControlAndBoundsText() throws Exception {
        try (MockWebServer server = new MockWebServer()) {
            char[] chars = new char[200000]; java.util.Arrays.fill(chars, 'L');
            String control = new String(chars);
            server.enqueue(new MockResponse().withWebSocketUpgrade(new WebSocketListener() {
                @Override public void onOpen(WebSocket ws, Response r) { ws.send(control); }
                @Override public void onMessage(WebSocket ws, String text) { ws.send(new String(new char[1024 * 1024 + 1])); }
            }));
            XyzWebSocket socket = client(server);
            try {
                assertTrue(socket.connect("/lan?queue"));
                byte[] e = next(socket); if (e[0] == 0) e = next(socket);
                assertEquals(1, e[0]); assertEquals(control, new String(e, 4, e.length - 4, StandardCharsets.UTF_8));
                assertTrue(socket.send("next".getBytes(StandardCharsets.UTF_8), true));
                e = next(socket); assertEquals(3, e[0]); assertEquals(1009, (e[1] & 255) * 256 + (e[2] & 255));
            } finally { socket.close(); }
        }
    }
    @Test public void refusesRedirect() throws Exception {
        try (MockWebServer server = new MockWebServer()) {
            server.enqueue(new MockResponse().setResponseCode(302).addHeader("Location", "/unexpected"));
            XyzWebSocket socket = client(server);
            try {
                assertTrue(socket.connect("/lan?queue")); assertEquals(3, next(socket)[0]);
                assertEquals(1, server.getRequestCount());
            } finally { socket.close(); }
        }
    }
}
