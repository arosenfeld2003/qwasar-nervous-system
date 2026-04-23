use serde::{Deserialize, Serialize};
use thiserror::Error;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DeviceEvent {
    pub device_id: String,
    pub protocol: Protocol,
    pub event_type: String,
    pub timestamp_ms: u64,
    pub payload: serde_json::Value,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Protocol {
    Mqtt,
    Zigbee,
    ZWave,
    Http,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AlarmDecision {
    pub device_id: String,
    pub alarm_type: AlarmType,
    pub severity: Severity,
    pub message: String,
    pub triggered_at_ms: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum AlarmType {
    Motion,
    Intrusion,
    Fire,
    Flood,
    Custom(String),
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Severity {
    Low,
    Medium,
    High,
    Critical,
}

#[derive(Debug, Error)]
pub enum NervousSystemError {
    #[error("serialization error: {0}")]
    Serialization(#[from] serde_json::Error),
    #[error("io error: {0}")]
    Io(#[from] std::io::Error),
}
