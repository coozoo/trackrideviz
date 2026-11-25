package io.github.coozoo.trackrideviz;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;

// Downloads over Android's own HTTPS, for the program's map tiles: Qt for Android has no TLS
// backend of its own here. Called from a worker thread, it waits for the answer.
public class Http
{
    // The body of a GET of `address`, or null when it failed or the server said no.
    public static byte[] get(String address, String userAgent, int timeoutMs)
    {
        HttpURLConnection connection = null;
        try {
            connection = (HttpURLConnection) new URL(address).openConnection();
            connection.setConnectTimeout(timeoutMs);
            connection.setReadTimeout(timeoutMs);
            connection.setRequestProperty("User-Agent", userAgent);
            if (connection.getResponseCode() != HttpURLConnection.HTTP_OK)
                return null;
            InputStream in = connection.getInputStream();
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[16384];
            for (int read; (read = in.read(buffer)) != -1; )
                out.write(buffer, 0, read);
            in.close();
            return out.toByteArray();
        } catch (Exception e) {
            return null;
        } finally {
            if (connection != null)
                connection.disconnect();
        }
    }
}
