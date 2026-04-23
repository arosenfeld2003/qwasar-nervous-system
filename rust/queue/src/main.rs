use anyhow::Result;
use futures_lite::StreamExt;
use lapin::{options::*, types::FieldTable, Connection, ConnectionProperties};
use nervous_system_common::DeviceEvent;
use tracing::{error, info};

const AMQP_URI: &str = "amqp://guest:guest@localhost:5672";
const INBOUND_QUEUE: &str = "device.events.raw";
const OUTBOUND_QUEUE: &str = "device.events.normalized";

#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "nervous_system_queue=info".into()),
        )
        .init();

    info!("Starting event queue service");

    let conn = Connection::connect(AMQP_URI, ConnectionProperties::default()).await?;
    let channel = conn.create_channel().await?;

    channel
        .queue_declare(
            INBOUND_QUEUE,
            QueueDeclareOptions {
                durable: true,
                ..Default::default()
            },
            FieldTable::default(),
        )
        .await?;

    channel
        .queue_declare(
            OUTBOUND_QUEUE,
            QueueDeclareOptions {
                durable: true,
                ..Default::default()
            },
            FieldTable::default(),
        )
        .await?;

    info!("Queues declared, consuming from {}", INBOUND_QUEUE);

    let consumer = channel
        .basic_consume(
            INBOUND_QUEUE,
            "queue-service",
            BasicConsumeOptions::default(),
            FieldTable::default(),
        )
        .await?;

    let publish_channel = conn.create_channel().await?;

    let mut consumer = consumer;
    while let Some(delivery) = consumer.next().await {
        match delivery {
            Ok(delivery) => {
                match serde_json::from_slice::<DeviceEvent>(&delivery.data) {
                    Ok(event) => {
                        info!(device_id = %event.device_id, event_type = %event.event_type, "received event");
                        forward_event(&publish_channel, &event).await?;
                        delivery.ack(BasicAckOptions::default()).await?;
                    }
                    Err(e) => {
                        error!("failed to deserialize event: {}", e);
                        delivery.nack(BasicNackOptions::default()).await?;
                    }
                }
            }
            Err(e) => error!("consumer error: {}", e),
        }
    }

    Ok(())
}

async fn forward_event(channel: &lapin::Channel, event: &DeviceEvent) -> Result<()> {
    let payload = serde_json::to_vec(event)?;
    channel
        .basic_publish(
            "",
            OUTBOUND_QUEUE,
            BasicPublishOptions::default(),
            &payload,
            lapin::BasicProperties::default().with_delivery_mode(2),
        )
        .await?
        .await?;
    Ok(())
}
