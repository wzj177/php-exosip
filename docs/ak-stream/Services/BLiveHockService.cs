using System;
using System.Collections.Generic;
using LibCommon;
using LibLogger;
using AKStreamWeb.Models;
using AKStreamWeb.Misc;
using LibCommon.Structs.GB28181;
using AKStreamWeb.Helper;

namespace AKStreamWeb.Services
{
  public static class BLiveHockService
  {
    private static string LoggerHead = "BLiveHock";


    public static async void Hock(string hockType, string json)
    {
      if (!checkIsEnable())
      {
        return;
      }

      switch (hockType)
      {
        case "SipOnUnRegisterReceived":
          OnUnRegisterReceived(json);
          break;
        case "SipOnKeepaliveReceived":
          OnKeepaliveReceived(json);
          break;
        case "SipOnDeviceReadyReceived":
          OnDeviceReadyReceived(json);
          break;
        case "SipOnDeviceStatusReceived":
          OnDeviceStatusReceived(json);
          break;
        case "SipOnInviteHistoryVideoFinished":
          OnInviteHistoryVideoFinished(json);
          break;
        case "SipOnCatalogReceived":
          OnCatalogReceived(json);
          break;
        case "WebHockOnPlay":
          onStreamPlay(json);
          break;
        case "AutoTaskOther":
         onAutoTaskOther(json);
         break;
        case "WebHockOnRecordMp4":
        onRecordMp4(json);
        break;
        default:
          OnRegisterReceived(json);
          break;
      }
    }

    private static async void onRecordMp4(string json)
    {
      string url = generalUrl("WebHockOnRecordMp4");
      try
      {
        var res = NetHelper.HttpPostRequestAsync(url, getDefaultHeaders(), json);
        GCommon.Logger.Info($"[{LoggerHead}]->收到WebHook-OnRecordMp4回调->发送给业务服务端成功");
      }
      catch (Exception ex)
      {
        GCommon.Logger.Error(
            $"[{LoggerHead}]->收到WebHook-OnRecordMp4回调->发送给业务服务端失败->\r\n{ex.Message}\r\n{ex.StackTrace}");
      }
    }

    private static async void onAutoTaskOther(string json)
    {
      string url = generalUrl("AutoTaskOther");
      try
      {
        var res = NetHelper.HttpPostRequestAsync(url, getDefaultHeaders(), json);
        GCommon.Logger.Info($"[{LoggerHead}]->30秒任务->清理任务回调发送成功");
      }
      catch (Exception ex)
      {
        GCommon.Logger.Error(
            $"[{LoggerHead}]->30秒任务->清理任务回调发送失败->\r\n{ex.Message}\r\n{ex.StackTrace}");
      }
    }

    private static async void onStreamPlay(string json)
    {
      string url = generalUrl("WebHockOnPlay");
      try
      {
        var res = NetHelper.HttpPostRequestAsync(url, getDefaultHeaders(), json);
        GCommon.Logger.Info($"[{LoggerHead}]->直播有播放者进入->发送成功");
      }
      catch (Exception ex)
      {
        GCommon.Logger.Error(
            $"[{LoggerHead}]->直播有播放者进入->发送失败->\r\n{ex.Message}\r\n{ex.StackTrace}");
      }
    }

    private static async void OnRegisterReceived(string sipDeviceJson)
    {
      string url = generalUrl("SipOnRegisterReceived");
      try
      {
        var res = NetHelper.HttpPostRequestAsync(url, getDefaultHeaders(), sipDeviceJson);
        GCommon.Logger.Info($"[{LoggerHead}]->设备注册->发送成功");
      }
      catch (Exception ex)
      {
        GCommon.Logger.Error(
            $"[{LoggerHead}]->设备注册->发送失败->\r\n{ex.Message}\r\n{ex.StackTrace}");
      }
    }

    private static async void OnUnRegisterReceived(string json)
    {
      string url = generalUrl("SipOnUnRegisterReceived");
      try
      {
        var res = NetHelper.HttpPostRequestAsync(url, getDefaultHeaders(), json);
        GCommon.Logger.Info($"[{LoggerHead}]->设备注销->发送成功");
      }
      catch (Exception ex)
      {
        GCommon.Logger.Error(
            $"[{LoggerHead}]->设备注销->发送失败->\r\n{ex.Message}\r\n{ex.StackTrace}");
      }
    }

    private static async void OnKeepaliveReceived(string json)
    {
      string url = generalUrl("SipOnKeepaliveReceived");
      try
      {
        var res = NetHelper.HttpPostRequestAsync(url, getDefaultHeaders(), json);
        GCommon.Logger.Info($"[{LoggerHead}]->设备有心跳时->发送成功");
      }
      catch (Exception ex)
      {
        GCommon.Logger.Error(
            $"[{LoggerHead}]->设备有心跳时->发送失败->\r\n{ex.Message}\r\n{ex.StackTrace}");
      }
    }

    private static async void OnDeviceReadyReceived(string json)
    {
      string url = generalUrl("SipOnDeviceReadyReceived");
      try
      {
        var res = NetHelper.HttpPostRequestAsync(url, getDefaultHeaders(), json);
        GCommon.Logger.Info($"[{LoggerHead}]->设备就绪->发送成功");
      }
      catch (Exception ex)
      {
        GCommon.Logger.Error(
            $"[{LoggerHead}]->设备就绪->发送失败->\r\n{ex.Message}\r\n{ex.StackTrace}");
      }
    }

    private static async void OnDeviceStatusReceived(string json)
    {
      string url = generalUrl("SipOnDeviceStatusReceived");
      try
      {
        var res = NetHelper.HttpPostRequestAsync(url, getDefaultHeaders(), json);
        GCommon.Logger.Info($"[{LoggerHead}]->获取到设备状态时->发送成功");
      }
      catch (Exception ex)
      {
        GCommon.Logger.Error(
            $"[{LoggerHead}]->获取到设备状态时->发送失败->\r\n{ex.Message}\r\n{ex.StackTrace}");
      }
    }

    private static async void OnInviteHistoryVideoFinished(string json)
    {
      string url = generalUrl("SipOnInviteHistoryVideoFinished");
      try
      {
        var res = NetHelper.HttpPostRequestAsync(url, getDefaultHeaders(), json);
        GCommon.Logger.Info($"[{LoggerHead}]->收到设备的录像文件列表时->发送成功");
      }
      catch (Exception ex)
      {
        GCommon.Logger.Error(
            $"[{LoggerHead}]->收到设备的录像文件列表时->发送失败->\r\n{ex.Message}\r\n{ex.StackTrace}");
      }
    }

    private static async void OnCatalogReceived(string json)
    {
      string url = generalUrl("SipOnCatalogReceived");
      try
      {
        var res = NetHelper.HttpPostRequestAsync(url, getDefaultHeaders(), json);
        GCommon.Logger.Info($"[{LoggerHead}]->收到设备目录通知->发送成功");
      }
      catch (Exception ex)
      {
        GCommon.Logger.Error(
            $"[{LoggerHead}]->收到设备目录通知->发送失败->\r\n{ex.Message}\r\n{ex.StackTrace}");
      }
    }

    private static string generalUrl(string hockType)
    {
      var bLiveHock = Common.AkStreamWebConfig.BLiveHock;
      string url = "";
      switch (hockType)
      {
        case "SipOnUnRegisterReceived":
          url = bLiveHock.SipOnUnRegisterReceived.ToString();
          break;
        case "SipOnKeepaliveReceived":
          url = bLiveHock.SipOnKeepaliveReceived.ToString();
          break;
        case "SipOnDeviceReadyReceived":
          url = bLiveHock.SipOnDeviceReadyReceived.ToString();
          break;
        case "SipOnDeviceStatusReceived":
          url = bLiveHock.SipOnDeviceStatusReceived.ToString();
          break;
        case "SipOnInviteHistoryVideoFinished":
          url = bLiveHock.SipOnInviteHistoryVideoFinished.ToString();
          break;
        case "SipOnCatalogReceived":
          url = bLiveHock.SipOnCatalogReceived.ToString();
          break;
        case "WebHockOnPlay":
          url = bLiveHock.WebHockOnPlay.ToString();
          break;
        case "AutoTaskOther":
          url = bLiveHock.AutoTaskOther.ToString();
          break;
        case "WebHockOnRecordMp4":
          url = bLiveHock.WebHockOnRecordMp4.ToString();
          break;
        default:
          url = bLiveHock.SipOnRegisterReceived.ToString();
          break;
      }

      return string.Format("{0}{1}", bLiveHock.ApiHost, url);
    }


    private static Dictionary<string, string> getDefaultHeaders()
    {
      var headers = new Dictionary<string, string>();
      var userAgent = "AKStream BLive Hock/1.0.0";
      var accessKey = Common.AkStreamWebConfig.AccessKey;
      var authSign = makeSignStr(userAgent, accessKey);

      headers.Add("User-Agent", userAgent);
      headers.Add("X-Auth-Sign", authSign);

      return headers;
    }

    private static string makeSignStr(string userAgent, string accessKey)
    {
      var str = string.Format("{0}\n{1}", userAgent, accessKey);
      var md5Str = Md5Helper.md5(str);

      return Base64Helper.DesEncrypt(md5Str);
    }

    private static bool checkIsEnable()
    {
      return Common.AkStreamWebConfig.BLiveHock.Enable;
    }
  }
}