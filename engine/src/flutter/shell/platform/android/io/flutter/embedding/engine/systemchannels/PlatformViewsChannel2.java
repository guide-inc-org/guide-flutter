// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package io.flutter.embedding.engine.systemchannels;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import io.flutter.Log;
import io.flutter.embedding.engine.dart.DartExecutor;
import io.flutter.plugin.common.MethodCall;
import io.flutter.plugin.common.MethodChannel;
import io.flutter.plugin.common.StandardMethodCodec;
import java.nio.ByteBuffer;
import java.util.List;
import java.util.Map;

/**
 * System channel that sends 2-way communication between Flutter and Android to facilitate embedding
 * of Android Views within a Flutter application.
 *
 * <p>Register a {@link PlatformViewsHandler} to implement the Android side of this channel.
 */
public class PlatformViewsChannel2 {
  private static final String TAG = "PlatformViewsChannel2";

  private final MethodChannel channel;
  private PlatformViewsHandler handler;

  /**
   * Constructs a {@code PlatformViewsChannel} that connects Android to the Dart code running in
   * {@code dartExecutor}.
   *
   * <p>The given {@code dartExecutor} is permitted to be idle or executing code.
   *
   * <p>See {@link DartExecutor}.
   */
  public PlatformViewsChannel2(@NonNull DartExecutor dartExecutor) {
    channel =
        new MethodChannel(dartExecutor, "flutter/platform_views_2", StandardMethodCodec.INSTANCE);
    channel.setMethodCallHandler(parsingHandler);
  }

  public void invokeViewFocused(int viewId) {
    if (channel == null) {
      return;
    }
    channel.invokeMethod("viewFocused", viewId);
  }

  private static String detailedExceptionString(Exception exception) {
    return Log.getStackTraceString(exception);
  }

  private final MethodChannel.MethodCallHandler parsingHandler =
      new MethodChannel.MethodCallHandler() {
        @Override
        public void onMethodCall(@NonNull MethodCall call, @NonNull MethodChannel.Result result) {
          // If there is no handler to respond to this message then we don't need to
          // parse it. Return.
          if (handler == null) {
            return;
          }

          Log.v(TAG, "Received '" + call.method + "' message.");
          switch (call.method) {
            case "create":
              create(call, result);
              break;
            case "dispose":
              dispose(call, result);
              break;
            case "touch":
              touch(call, result);
              break;
            case "setDirection":
              setDirection(call, result);
              break;
            case "clearFocus":
              clearFocus(call, result);
              break;
            case "isSurfaceControlEnabled":
              isSurfaceControlEnabled(call, result);
              break;
            default:
              result.notImplemented();
          }
        }

        private void create(@NonNull MethodCall call, @NonNull MethodChannel.Result result) {
          final Map<String, Object> createArgs = call.arguments();
          final ByteBuffer additionalParams =
              createArgs.containsKey("params")
                  ? ByteBuffer.wrap((byte[]) createArgs.get("params"))
                  : null;
          try {
            final PlatformViewCreationRequest request =
                PlatformViewCreationRequest.createHCPPRequest(
                    (int) createArgs.get("id"),
                    (String) createArgs.get("viewType"),
                    (int) createArgs.get("direction"),
                    additionalParams);
            handler.createPlatformView(request);
            result.success(null);

          } catch (IllegalStateException exception) {
            result.error("error", detailedExceptionString(exception), null);
          }
        }

        private void dispose(@NonNull MethodCall call, @NonNull MethodChannel.Result result) {
          Map<String, Object> disposeArgs = call.arguments();
          int viewId = (int) disposeArgs.get("id");

          try {
            handler.dispose(viewId);
            result.success(null);
          } catch (IllegalStateException exception) {
            result.error("error", detailedExceptionString(exception), null);
          }
        }

        private void touch(@NonNull MethodCall call, @NonNull MethodChannel.Result result) {
          List<Object> args = call.arguments();
          PlatformViewTouch touch =
              new PlatformViewTouch(
                  (int) args.get(0),
                  (Number) args.get(1),
                  (Number) args.get(2),
                  (int) args.get(3),
                  (int) args.get(4),
                  args.get(5),
                  args.get(6),
                  (int) args.get(7),
                  (int) args.get(8),
                  (float) (double) args.get(9),
                  (float) (double) args.get(10),
                  (int) args.get(11),
                  (int) args.get(12),
                  (int) args.get(13),
                  (int) args.get(14),
                  ((Number) args.get(15)).longValue());

          try {
            handler.onTouch(touch);
            result.success(null);
          } catch (IllegalStateException exception) {
            result.error("error", detailedExceptionString(exception), null);
          }
        }

        private void setDirection(@NonNull MethodCall call, @NonNull MethodChannel.Result result) {
          Map<String, Object> setDirectionArgs = call.arguments();
          int newDirectionViewId = (int) setDirectionArgs.get("id");
          int direction = (int) setDirectionArgs.get("direction");

          try {
            handler.setDirection(newDirectionViewId, direction);
            result.success(null);
          } catch (IllegalStateException exception) {
            result.error("error", detailedExceptionString(exception), null);
          }
        }

        private void clearFocus(@NonNull MethodCall call, @NonNull MethodChannel.Result result) {
          int viewId = call.arguments();
          try {
            handler.clearFocus(viewId);
            result.success(null);
          } catch (IllegalStateException exception) {
            result.error("error", detailedExceptionString(exception), null);
          }
        }

        private void isSurfaceControlEnabled(
            @NonNull MethodCall call, @NonNull MethodChannel.Result result) {
          result.success(handler.isSurfaceControlEnabled());
        }
      };

  /**
   * Sets the {@link PlatformViewsHandler} which receives all events and requests that are parsed
   * from the underlying platform views channel.
   */
  public void setPlatformViewsHandler(@Nullable PlatformViewsHandler handler) {
    this.handler = handler;
  }

  /**
   * Handler that receives platform view messages sent from Flutter to Android through a given
   * {@link PlatformViewsChannel}.
   *
   * <p>To register a {@code PlatformViewsHandler} with a {@link PlatformViewsChannel2}, see {@link
   * PlatformViewsChannel2#setPlatformViewsHandler(PlatformViewsHandler)}.
   */
  public interface PlatformViewsHandler {
    /**
     * The Flutter application would like to display a new Android {@code View}, i.e., platform
     * view.
     */
    void createPlatformView(@NonNull PlatformViewCreationRequest request);

    /** The Flutter application would like to dispose of an existing Android {@code View}. */
    void dispose(int viewId);

    /**
     * The user touched a platform view within Flutter.
     *
     * <p>Touch data is reported in {@code touch}.
     */
    void onTouch(@NonNull PlatformViewTouch touch);

    /**
     * The Flutter application would like to change the layout direction of an existing Android
     * {@code View}, i.e., platform view.
     */
    void setDirection(int viewId, int direction);

    /** Clears the focus from the platform view with a give id if it is currently focused. */
    void clearFocus(int viewId);

    /** Whether the SurfaceControl swapchain is enabled. */
    boolean isSurfaceControlEnabled();
  }
}
