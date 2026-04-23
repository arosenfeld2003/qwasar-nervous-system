use anyhow::Result;
use futures_lite::StreamExt;
use lapin::{options::*, types::FieldTable, Connection, ConnectionProperties};
use nervous_system_common::AlarmDecision;
use tracing::{error, info};

const AMQP_URI: &str = "amqp://guest:guest@localhost:5672";
const ALARM_QUEUE: &str = "alarms.dispatch";

#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "nervous_system_dispatcher=info".into()),
        )
        .init();

    info!("Starting alarm dispatcher");

    let conn = Connection::connect(AMQP_URI, ConnectionProperties::default()).await?;
    let channel = conn.create_channel().await?;

    channel
        .queue_declare(
            ALARM_QUEUE,
            QueueDeclareOptions {
                durable: true,
                ..Default::default()
            },
            FieldTable::default(),
        )
        .await?;

    info!("Consuming alarms from {}", ALARM_QUEUE);

    let consumer = channel
        .basic_consume(
            ALARM_QUEUE,
            "dispatcher-service",
            BasicConsumeOptions::default(),
            FieldTable::default(),
        )
        .await?;

    let mut consumer = consumer;
    while let Some(delivery) = consumer.next().await {
        match delivery {
            Ok(delivery) => {
                match serde_json::from_slice::<AlarmDecision>(&delivery.data) {
                    Ok(alarm) => {
                        info!(device_id = %alarm.device_id, message = %alarm.message, "dispatching alarm");
                        // TODO: start Temporal workflow for durable alarm delivery
                        dispatch_alarm(&alarm).await;
                        delivery.ack(BasicAckOptions::default()).await?;
                    }
                    Err(e) => {
                        error!("failed to deserialize alarm: {}", e);
                        delivery.nack(BasicNackOptions::default()).await?;
                    }
                }
            }
            Err(e) => error!("consumer error: {}", e),
        }
    }

    Ok(())
}

async fn dispatch_alarm(alarm: &AlarmDecision) {
    // TODO: implement via Temporal workflow activities:
    //   - send_http_push(alarm)
    //   - send_email(alarm)
    //   - send_sms(alarm)
    info!(device_id = %alarm.device_id, "alarm dispatched (stub)");
}
