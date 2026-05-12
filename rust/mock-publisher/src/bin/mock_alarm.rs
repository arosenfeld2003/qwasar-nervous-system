use anyhow::Result;
use lapin::{options::*, types::FieldTable, BasicProperties, Connection, ConnectionProperties};
use nervous_system_common::{AlarmDecision, AlarmType, Severity};
use std::time::{SystemTime, UNIX_EPOCH};
use tokio::time::{sleep, Duration};
use tracing::info;

const QUEUE: &str = "alarms.dispatch";

fn now_ms() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_millis() as u64
}

fn alarm_templates() -> Vec<AlarmDecision> {
    vec![
        AlarmDecision {
            device_id: "sim-mqtt-001".into(),
            alarm_type: AlarmType::Motion,
            severity: Severity::Medium,
            message: "Motion detected at front-door zone".into(),
            triggered_at_ms: now_ms(),
        },
        AlarmDecision {
            device_id: "sim-mqtt-002".into(),
            alarm_type: AlarmType::Fire,
            severity: Severity::Critical,
            message: "Smoke detected in kitchen — possible fire".into(),
            triggered_at_ms: now_ms(),
        },
        AlarmDecision {
            device_id: "sim-zigbee-001".into(),
            alarm_type: AlarmType::Intrusion,
            severity: Severity::High,
            message: "Main entrance door opened outside schedule".into(),
            triggered_at_ms: now_ms(),
        },
        AlarmDecision {
            device_id: "sim-zigbee-002".into(),
            alarm_type: AlarmType::Flood,
            severity: Severity::High,
            message: "Flood sensor triggered in basement (12 mm water level)".into(),
            triggered_at_ms: now_ms(),
        },
    ]
}

#[tokio::main]
async fn main() -> Result<()> {
    dotenvy::dotenv().ok();
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "mock_alarm=info".into()),
        )
        .init();

    let amqp_uri = std::env::var("AMQP_URI")
        .unwrap_or_else(|_| "amqp://guest:guest@localhost:5672".into());
    let interval_ms: u64 = std::env::var("PUBLISH_INTERVAL_MS")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(3000);

    info!("connecting to RabbitMQ at {}", amqp_uri);
    let conn = Connection::connect(&amqp_uri, ConnectionProperties::default()).await?;
    let channel = conn.create_channel().await?;

    channel
        .queue_declare(
            QUEUE,
            QueueDeclareOptions {
                durable: true,
                ..Default::default()
            },
            FieldTable::default(),
        )
        .await?;

    info!("publishing mock alarms every {}ms — Ctrl-C to stop", interval_ms);

    let count = alarm_templates().len();

    for (i, _) in std::iter::repeat(()).enumerate() {
        let mut alarm = alarm_templates().remove(i % count);
        alarm.triggered_at_ms = now_ms();

        let body = serde_json::to_vec(&alarm)?;
        channel
            .basic_publish(
                "",
                QUEUE,
                BasicPublishOptions::default(),
                &body,
                BasicProperties::default().with_delivery_mode(2),
            )
            .await?
            .await?;

        info!(
            device_id = %alarm.device_id,
            message = %alarm.message,
            "published alarm"
        );

        sleep(Duration::from_millis(interval_ms)).await;
    }

    Ok(())
}
