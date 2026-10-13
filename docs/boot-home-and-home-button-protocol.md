# 开机功能首页与 Home 键通讯流程

本文记录本次固件修改后的实际行为。首页是“AI 听书 / 小剧场”选择页。

## 1. 开机显示首页

设备完成显示初始化后立即显示功能首页；网络和协议激活完成后会再次确认该页面处于最前层。

显示首页是板端本地 UI 初始化，不发送业务消息，也不会自动创建互动听书或小剧场会话。

## 2. 点击 AI 听书

点击后只发送互动听书入口消息，不由页面入口主动发送 `listen/start`。

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "enter",
  "data": {
    "request_id": "req_home_1"
  }
}
```

板端等待匹配的 `home_result` 后显示互动听书首页。后端开始播报引导语；播报结束后，设备沿用原有 TTS 状态机自动进入监听。

## 3. 点击小剧场

点击后只发送：

```json
{
  "type": "anti_fraud",
  "version": 2,
  "action": "start",
  "data": {}
}
```

页面入口不主动发送 `listen/start`。后端播报结束后，设备沿用原有 TTS 状态机自动进入监听。

## 4. 互动听书中点击 Home

Home 是强制返回功能首页，不显示退出确认弹窗。

### 4.1 立即打断当前 TTS

板端先通过既有通用协议发送：

```json
{
  "session_id": "当前通用会话ID",
  "type": "abort"
}
```

### 4.2 请求退出互动听书

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "exit",
  "data": {}
}
```

发送后板端立即清理当前选项、确认弹窗、节点和待处理请求，并显示“AI 听书 / 小剧场”首页。

### 4.3 后台完成退出确认

服务端仍按现有 V2 协议下发：

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "show_confirmation",
  "data": {
    "session_id": "ips_01k_story",
    "confirmation_id": "exit:8f32a91bc614",
    "purpose": "exit_confirm",
    "title": "退出互动听书",
    "prompt": "是否退出互动听书？"
  }
}
```

板端识别到这是 Home 发起的强制退出，不显示弹窗，自动回传：

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "confirm",
  "data": {
    "session_id": "ips_01k_story",
    "confirmation_id": "exit:8f32a91bc614",
    "confirmed": true
  }
}
```

服务端最后下发：

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "clear_confirmation",
  "data": {
    "session_id": "ips_01k_story",
    "confirmation_id": "exit:8f32a91bc614",
    "reason": "selected"
  }
}
```

板端只完成后台状态收敛，页面继续停留在功能首页。

## 5. 小剧场中点击 Home

板端先发送通用 `abort` 打断语音：

```json
{
  "session_id": "当前通用会话ID",
  "type": "abort"
}
```

随后发送小剧场业务退出消息：

```json
{
  "type": "anti_fraud",
  "version": 2,
  "action": "exit",
  "request_id": "本次 Home 操作生成的唯一请求号",
  "data": {
    "session_id": "afs_xxx"
  }
}
```

板端发送后立即清理小剧场页面和状态并返回功能首页，不等待服务端回包。服务端应按
`request_id` 幂等结束对应 `session_id` 的任务，并停止继续下发旧 TTS 和选项。

## 6. 页面保护

Home 返回后，板端会忽略迟到的互动听书 `home_result`、`show_options` 和普通 `show_confirmation`，防止旧消息重新覆盖功能首页。只有对应本次强制退出的 `exit_confirm` 会在后台处理。
