package org.nfsu2.quest;

import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import java.util.Arrays;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

/** Called only by the native credential worker. No secrets enter logs. */
final class XyzCredentials {
    private static final String ALIAS="nfsu2.xyz.profile.credentials.v1";
    static synchronized byte[] crypt(String profile,byte[] input,boolean encrypt) {
        if(profile==null || !profile.matches("[0-9a-f]{32}") || input==null || input.length==0 || input.length>4096)return null;
        try {
            KeyStore store=KeyStore.getInstance("AndroidKeyStore");store.load(null);
            if(!store.containsAlias(ALIAS)) {
                if(!encrypt)return null;
                KeyGenerator generator=KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES,"AndroidKeyStore");
                generator.init(new KeyGenParameterSpec.Builder(ALIAS,KeyProperties.PURPOSE_ENCRYPT|KeyProperties.PURPOSE_DECRYPT)
                    .setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                    .setKeySize(256).build());generator.generateKey();
            }
            SecretKey key=(SecretKey)store.getKey(ALIAS,null);Cipher cipher=Cipher.getInstance("AES/GCM/NoPadding");
            if(encrypt) {
                cipher.init(Cipher.ENCRYPT_MODE,key);cipher.updateAAD(profile.getBytes(StandardCharsets.US_ASCII));
                byte[] iv=cipher.getIV();if(iv.length!=12)return null;
                byte[] body=cipher.doFinal(input),out=new byte[13+body.length];out[0]=1;
                System.arraycopy(iv,0,out,1,12);System.arraycopy(body,0,out,13,body.length);return out;
            }
            if(input.length<29 || input[0]!=1)return null;
            cipher.init(Cipher.DECRYPT_MODE,key,new GCMParameterSpec(128,input,1,12));
            cipher.updateAAD(profile.getBytes(StandardCharsets.US_ASCII));return cipher.doFinal(input,13,input.length-13);
        } catch(Exception ignored) { return null; }
        finally { Arrays.fill(input,(byte)0); }
    }
}
