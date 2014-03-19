#include "gnome.h"

Gnome::Gnome()
    : partition(NULL),
      options(NULL),
      metric("Lateness"),
      cluster_leaves(NULL),
      cluster_root(NULL),
      max_metric_process(-1),
      top_processes(QList<int>()),
      alternation(true),
      neighbors(-1),
      saved_messages(QSet<Message *>()),
      drawnPCs(QMap<PartitionCluster *, QRect>()),
      drawnNodes(QMap<PartitionCluster *, QRect>())
{
}

Gnome::~Gnome()
{
    if (cluster_root) {
        cluster_root->delete_tree();
        delete cluster_root;
        delete cluster_leaves;
    }
}

bool Gnome::detectGnome(Partition * part)
{
    Q_UNUSED(part);
    return false;
}

Gnome * Gnome::create()
{
    return new Gnome();
}

void Gnome::preprocess()
{
    findClusters();
    generateTopProcesses();
}

void Gnome::findClusters()
{
    // Calculate initial distances
    std::cout << "Finding clusters..." << std::endl;
    QList<DistancePair> distances;
    QList<int> processes = partition->events->keys();
    top_processes.clear();
    if (cluster_root)
    {
        cluster_root->delete_tree();
        delete cluster_root;
        delete cluster_leaves;
    }
    cluster_leaves = new QMap<int, PartitionCluster *>();
    long long int max_metric = LLONG_MIN;
    max_metric_process = -1;
    qSort(processes);
    int num_processes = processes.size();
    int p1, p2;
    long long int distance;
    for (int i = 0; i < num_processes; i++)
    {
        p1 = processes[i];
        //std::cout << "Calculating distances for process " << p1 << std::endl;
        cluster_leaves->insert(p1, new PartitionCluster(p1, partition->events->value(p1), "Lateness"));
        if (cluster_leaves->value(p1)->max_metric > max_metric)
        {
            max_metric = cluster_leaves->value(p1)->max_metric;
            max_metric_process = p1;
        }
        for (int j = i + 1; j < num_processes; j++)
        {
            p2 = processes[j];
            //std::cout << "     Calculating between " << p1 << " and " << p2 << std::endl;
            distance = calculateMetricDistance(partition->events->value(p1),
                                               partition->events->value(p2));
            distances.append(DistancePair(distance, p1, p2));
            //std::cout << "    distance between " << p1 << " and " << p2 << " is " << distance << std::endl;
        }
    }
    qSort(distances);

    int lastp = distances[0].p1;
    QList<long long int> cluster_distances = QList<long long int>();
    PartitionCluster * pc = NULL;
    for (int i = 0; i < distances.size(); i++)
    {
        //std::cout << "Handling distance #" << i << std::endl;
        DistancePair current = distances[i];
        if (cluster_leaves->value(current.p1)->get_root() != cluster_leaves->value(current.p2)->get_root())
        {
            pc = new PartitionCluster(current.distance,
                                      cluster_leaves->value(current.p1)->get_root(),
                                      cluster_leaves->value(current.p2)->get_root());
            cluster_distances.append(current.distance);
            lastp = current.p1;
        }
    }
    cluster_root = cluster_leaves->value(lastp)->get_root();

    // From here we could now compress the ClusterEvent metrics (doing the four divides ahead of time)
    // but I'm going to retain the information for now and see how it goes
}

// When calculating distance between two event lists. When one is missing a step, we
// estimate the lateness of that missing step via the average of the two adjacent steps
// No wait, for now lets estimate the lateness as the step that came before it if available
// and only if not we estimate as the one afterwards or perhaps skip?
long long int Gnome::calculateMetricDistance(QList<Event *> * list1, QList<Event *> * list2)
{
    int index1 = 0, index2 = 0, total_calced_steps = 0;
    Event * evt1 = list1->at(0), * evt2 = list2->at(0);
    long long int last1 = 0, last2 = 0, total_difference = 0;

    while (evt1 && evt2)
    {
        if (evt1->step == evt2->step) // If they're equal, add their distance
        {
            last1 = evt1->getMetric(metric);
            last2 = evt2->getMetric(metric);
            total_difference += (last1 - last2) * (last1 - last2);
            ++total_calced_steps;
            // Increment both event lists now
            ++index2;
            if (index2 < list2->size())
                evt2 = list2->at(index2);
            else
                evt2 = NULL;
            ++index1;
            if (index1 < list1->size())
                evt1 = list1->at(index1);
            else
                evt1 = NULL;
        } else if (evt1->step > evt2->step) { // If not, increment steps until they match
            // Estimate evt1 lateness
            last2 = evt2->getMetric(metric);
            if (evt1->comm_prev && evt1->comm_prev->partition == evt1->partition)
            {
                total_difference += (last1 - last2) * (last1 - last2);
                ++total_calced_steps;
            }

            // Move evt2 forward
            ++index2;
            if (index2 < list2->size())
                evt2 = list2->at(index2);
            else
                evt2 = NULL;
        } else {
            last1 = evt1->getMetric(metric);
            if (evt2->comm_prev && evt2->comm_prev->partition == evt2->partition)
            {
                total_difference += (last1 - last2) * (last1 - last2);
                ++total_calced_steps;
            }

            // Move evt1 forward
            ++index1;
            if (index1 < list1->size())
                evt1 = list1->at(index1);
            else
                evt1 = NULL;
        }
    }
    if (total_calced_steps == 0) {
        //std::cout << "No steps between " << evt1->process << " and " << evt2->process << std::endl;
        return LLONG_MAX; //0;
    }
    //std::cout << "     total_difference " << total_difference << " with steps " << total_calced_steps << std::endl;
    return total_difference / total_calced_steps;
}

void Gnome::setNeighbors(int _neighbors)
{
    if (neighbors == _neighbors)
        return;

    neighbors = _neighbors;
    generateTopProcesses();
}

void Gnome::generateTopProcesses(PartitionCluster *pc)
{
    top_processes.clear();
    if (neighbors < 0)
        neighbors = 1;

    if (pc)
    {
        if (options->topByCentroid)
            generateTopProcessesWorker(findCentroidProcess(pc));
        else
            generateTopProcessesWorker(findMaxMetricProcess(pc));
    }
    else
        generateTopProcessesWorker(max_metric_process);
    qSort(top_processes);
}

void Gnome::generateTopProcessesWorker(int process)
{
    QList<Event *> * elist = NULL;
    QSet<int> add_processes = QSet<int>();
    add_processes.insert(process);
    QSet<int> new_processes = QSet<int>();
    QSet<int> current_processes = QSet<int>();
    current_processes.insert(process);
    for (int i = 0; i < neighbors; i++)
    {
        for (QSet<int>::Iterator proc = current_processes.begin(); proc != current_processes.end(); ++proc)
        {
            elist = partition->events->value(*proc);
            for (QList<Event *>::Iterator evt = elist->begin(); evt != elist->end(); ++evt)
            {
                for (QVector<Message *>::Iterator msg = (*evt)->messages->begin(); msg != (*evt)->messages->end(); ++msg)
                {
                    if (*evt == (*msg)->sender)
                    {
                        add_processes.insert((*msg)->receiver->process);
                        new_processes.insert((*msg)->receiver->process);
                    }
                    else
                    {
                        add_processes.insert((*msg)->sender->process);
                        new_processes.insert((*msg)->receiver->process);
                    }
                }
            }
        }
        current_processes.clear();
        current_processes += new_processes;
        new_processes.clear();
    }
    top_processes = add_processes.toList();
}

int Gnome::findMaxMetricProcess(PartitionCluster * pc)
{
    int p1, max_process;
    long long int max_metric = 0;
    for (int i = 0; i < pc->members->size(); i++)
    {
        p1 = pc->members->at(i);
        if (cluster_leaves->value(p1)->max_metric > max_metric)
        {
            max_metric = cluster_leaves->value(p1)->max_metric;
            max_process = p1;
        }
    }
    return max_process;
}

int Gnome::findCentroidProcess(PartitionCluster * pc)
{
    QList<CentroidDistance> distances = QList<CentroidDistance>();
    for (int i = 0; i < pc->members->size(); i++)
        distances.append(CentroidDistance(0, pc->members->at(i)));
    QList<AverageMetric> events = QList<AverageMetric>();
    for (QList<ClusterEvent *>::Iterator evt = pc->events->begin(); evt != pc->events->end(); ++evt)
    {
        events.append(AverageMetric((*evt)->getMetric() / (*evt)->getCount(), (*evt)->step));
    }

    int num_events = events.size();
    for (int i = 0; i < distances.size(); i++)
    {
        int index1 = 0, index2 = 0, total_calced_steps = 0;
        long long int last1 = 0, last2 = 0, total_difference = 0;
        int process = distances[i].process;
        Event * evt = partition->events->value(process)->first();

        while (evt && index2 < num_events)
        {
            AverageMetric am = events[index2];
            if (evt->step == am.step) // If they're equal, add their distance
            {
                last1 = evt->getMetric(metric);
                last2 = am.metric;
                total_difference += (last1 - last2) * (last1 - last2);
                ++total_calced_steps;
                // Increment both event lists now
                ++index2;
                ++index1;
                if (index1 < partition->events->value(process)->size())
                    evt = partition->events->value(process)->at(index1);
                else
                    evt = NULL;
            } else if (evt->step > am.step) { // If not, increment steps until they match
                // Estimate evt1 lateness
                last2 = am.metric;
                if (evt->comm_prev && evt->comm_prev->partition == evt->partition)
                {
                    total_difference += (last1 - last2) * (last1 - last2);
                    ++total_calced_steps;
                }

                // Move evt2 forward
                ++index2;
            } else {
                last1 = evt->getMetric(metric);
                if (index2 > 0)
                {
                    total_difference += (last1 - last2) * (last1 - last2);
                    ++total_calced_steps;
                }

                // Move evt1 forward
                ++index1;
                if (index1 < partition->events->value(process)->size())
                    evt = partition->events->value(process)->at(index1);
                else
                    evt = NULL;
            }
        }
        if (total_calced_steps == 0)
        {
            distances[i].distance = LLONG_MAX;
        }
        else
        {
            distances[i].distance = total_difference / total_calced_steps;
        }
    }
    qSort(distances);
    return distances[0].process;
}


void Gnome::drawGnomeQt(QPainter * painter, QRect extents, VisOptions *_options)
{
    options = _options;
    saved_messages.clear();
    drawnPCs.clear();
    drawnNodes.clear();

    drawGnomeQtCluster(painter, extents);
}

// The height allowed to the top processes. This is not the y but the height.
int Gnome::getTopHeight(QRect extents)
{
    int topHeight = 0;
    int fair_portion = top_processes.size() / 1.0 / cluster_root->members->size() * extents.height();
    int min_size = 12 * top_processes.size();
    if (min_size > extents.height())
        topHeight = fair_portion;
    else
        topHeight = std::max(fair_portion, min_size);

    int max_cluster_leftover = extents.height() - cluster_root->visible_clusters() * (3 * clusterMaxHeight);
    if (topHeight < max_cluster_leftover)
        topHeight = max_cluster_leftover;

    return topHeight;
}

void Gnome::drawGnomeQtCluster(QPainter * painter, QRect extents)
{
    alternation = true;


    int topHeight = getTopHeight(extents);

    int effectiveHeight = extents.height() - topHeight;
    int effectiveWidth = extents.width();

    int processSpan = partition->events->size();
    int stepSpan = partition->max_global_step - partition->min_global_step + 2;
    int spacingMinimum = 12;


    int process_spacing = 0;
    if (effectiveHeight / processSpan > spacingMinimum)
        process_spacing = 3;

    int step_spacing = 0;
    if (effectiveWidth / stepSpan + 1 > spacingMinimum)
        step_spacing = 3;


    float blockwidth;
    float blockheight = floor(effectiveHeight / processSpan);
    if (options->showAggregateSteps)
        blockwidth = floor(effectiveWidth / stepSpan);
    else
        blockwidth = floor(effectiveWidth / (ceil(stepSpan / 2.0)));
    float barheight = blockheight - process_spacing;
    float barwidth = blockwidth - step_spacing;
    painter->setPen(QPen(QColor(0, 0, 0)));

    QRect top_extents = QRect(extents.x(), effectiveHeight, extents.width(), topHeight);
    drawGnomeQtTopProcesses(painter, top_extents, blockwidth, barwidth);

    QRect cluster_extents = QRect(extents.x(), extents.y(), extents.width(), effectiveHeight);
    drawGnomeQtClusterBranch(painter, cluster_extents, cluster_root,
                             blockheight, blockwidth, barheight, barwidth);

    // Now that we have drawn all the events, we need to draw the leaf-cluster messages or the leaf-leaf
    // messages which are saved in saved_messages.
    drawGnomeQtInterMessages(painter, blockwidth, partition->min_global_step, extents.x());

}


void Gnome::drawGnomeQtTopProcesses(QPainter * painter, QRect extents,
                                            int blockwidth, int barwidth)
{
    int effectiveHeight = extents.height();

    int process_spacing = blockwidth - barwidth;
    int step_spacing = process_spacing;

    float x, y, w, h, xa, wa;
    float blockheight = floor(effectiveHeight / top_processes.size());
    int startStep = partition->min_global_step;
    if (options->showAggregateSteps)
    {
        startStep -= 1;
    }
    float barheight = blockheight - process_spacing;

    QSet<Message *> drawMessages = QSet<Message *>();
    painter->setPen(QPen(QColor(0, 0, 0)));
    QMap<int, int> processYs = QMap<int, int>();

    for (int i = 0; i < top_processes.size(); ++i)
    {
        QList<Event *> * event_list = partition->events->value(top_processes[i]);
        y =  floor(extents.y() + i * blockheight) + 1;
        //painter->drawText(extents.x() - 1, y + blockheight / 2, QString::number(top_processes[i]));
        processYs[top_processes[i]] = y;
        for (QList<Event *>::Iterator evt = event_list->begin(); evt != event_list->end(); ++evt)
        {
            if (options->showAggregateSteps)
                x = floor(((*evt)->step - startStep) * blockwidth) + 1 + extents.x();
            else
                x = floor(((*evt)->step - startStep) / 2 * blockwidth) + 1 + extents.x();
            w = barwidth;
            h = barheight;

            painter->fillRect(QRectF(x, y, w, h), QBrush(options->colormap->color((*evt)->getMetric(metric))));

            // Draw border but only if we're doing spacing, otherwise too messy
            if (step_spacing > 0 && process_spacing > 0)
                painter->drawRect(QRectF(x,y,w,h));

            // For selection
            // drawnEvents[*evt] = QRect(x, y, w, h);

            for (QVector<Message *>::Iterator msg = (*evt)->messages->begin(); msg != (*evt)->messages->end(); ++msg)
            {
                if (top_processes.contains((*msg)->sender->process) && top_processes.contains((*msg)->receiver->process))
                    drawMessages.insert((*msg));
                else if (*evt == (*msg)->sender)
                {
                    // send
                    if ((*msg)->sender->process > (*msg)->receiver->process)
                        painter->drawLine(x + w/2, y + h/2, x + w, y);
                    else
                        painter->drawLine(x + w/2, y + h/2, x + w, y + h);
                }
                else
                {
                    // recv
                    if ((*msg)->sender->process > (*msg)->receiver->process)
                        painter->drawLine(x + w/2, y + h/2, x, y + h);
                    else
                        painter->drawLine(x + w/2, y + h/2, x, y);
                }
            }

            if (options->showAggregateSteps) {
                xa = floor(((*evt)->step - startStep - 1) * blockwidth) + 1 + extents.x();
                wa = barwidth;

                painter->fillRect(QRectF(xa, y, wa, h), QBrush(options->colormap->color((*evt)->getMetric(metric, true))));

                if (step_spacing > 0 && process_spacing > 0)
                    painter->drawRect(QRectF(xa, y, wa, h));
            }

        }
    }

        // Messages
        // We need to do all of the message drawing after the event drawing
        // for overlap purposes
    if (options->showMessages)
    {
        if (top_processes.size() <= 32)
            painter->setPen(QPen(Qt::black, 2, Qt::SolidLine));
        else
            painter->setPen(QPen(Qt::black, 1, Qt::SolidLine));
        Event * send_event;
        Event * recv_event;
        QPointF p1, p2;
        w = barwidth;
        h = barheight;
        for (QSet<Message *>::Iterator msg = drawMessages.begin(); msg != drawMessages.end(); ++msg) {
            send_event = (*msg)->sender;
            recv_event = (*msg)->receiver;
            y = processYs[send_event->process];
            if (options->showAggregateSteps)
                x = floor((send_event->step - startStep) * blockwidth) + 1 + extents.x();
            else
                x = floor((send_event->step - startStep) / 2 * blockwidth) + 1 + extents.x();
            p1 = QPointF(x + w/2.0, y + h/2.0);
            y = processYs[recv_event->process];
            if (options->showAggregateSteps)
                x = floor((recv_event->step - startStep) * blockwidth) + 1 + extents.x();
            else
                x = floor((recv_event->step - startStep) / 2 * blockwidth) + 1 + extents.x();
            p2 = QPointF(x + w/2.0, y + h/2.0);
            painter->drawLine(p1, p2);
        }
    }
}

void Gnome::drawGnomeQtInterMessages(QPainter * painter, int blockwidth, int startStep, int startx)
{
    if (options->showAggregateSteps)
        startStep -= 1;
    painter->setPen(QPen(Qt::black, 1.5, Qt::SolidLine));
    for (QSet<Message *>::Iterator msg = saved_messages.begin(); msg != saved_messages.end(); ++msg)
    {
        int x1, y1, x2, y2;
        PartitionCluster * sender_pc = cluster_leaves->value((*msg)->sender->process)->get_closed_root();
        PartitionCluster * receiver_pc = cluster_leaves->value((*msg)->receiver->process)->get_closed_root();

        x1 = startx + blockwidth * ((*msg)->sender->step - startStep + 0.5);
        if (sender_pc->children->isEmpty())  // Sender is leaf
            y1 = sender_pc->extents.y() + sender_pc->extents.height() / 2;
        else if (sender_pc->extents.y() > receiver_pc->extents.y()) // Sender is lower cluster
            y1 = sender_pc->extents.y();
        else
            y1 = sender_pc->extents.y() + sender_pc->extents.height();

        x2 = startx + blockwidth * ((*msg)->receiver->step - startStep + 0.5);
        if (receiver_pc->children->isEmpty()) // Sender is leaf
            y2 = receiver_pc->extents.y() + receiver_pc->extents.height() / 2;
        else if (receiver_pc->extents.y() > sender_pc->extents.y()) // Sender is lower cluster
            y2 = receiver_pc->extents.y();
        else
            y2 = receiver_pc->extents.y() + receiver_pc->extents.height();

        painter->drawLine(x1, y1, x2, y2);
    }
}

void Gnome::drawQtTree(QPainter * painter, QRect extents)
{
    int labelwidth = 0;
    if (cluster_root->leaf_open())
    {
        painter->setFont(QFont("Helvetica", 10));
        QFontMetrics font_metrics = painter->fontMetrics();
        QString text = QString::number((*(std::max_element(cluster_root->members->begin(), cluster_root->members->end()))));

        // Determine bounding box of FontMetrics
        labelwidth = font_metrics.boundingRect(text).width();
    }
    int depth = cluster_root->max_open_depth();
    int branch_length = 5;
    if (depth == 0)
        branch_length = 0;
    else if (5 * depth < extents.width() - labelwidth)
        branch_length = (extents.width() - labelwidth) / depth;

    int topHeight = getTopHeight(extents);
    int effectiveHeight = extents.height() - topHeight;
    int processSpan = partition->events->size();
    float blockheight = floor(effectiveHeight / processSpan);

    int leafx = branch_length * depth + labelwidth;

    drawTreeBranch(painter, QRect(extents.x(), extents.y(), extents.width(), extents.height() - topHeight),
                   cluster_root, branch_length, labelwidth, blockheight, leafx);
}


void Gnome::drawTreeBranch(QPainter * painter, QRect current, PartitionCluster * pc,
                                             int branch_length, int labelwidth, int blockheight, int leafx)
{
    //std::cout << "Drawing for cluster " << pc->memberString().toStdString().c_str() << std::endl;
    int pc_size = pc->members->size();
    //std::cout << "PC size is " << pc_size << " and blockheight is " << blockheight << std::endl;
    int my_x = current.x();
    int top_y = current.y();
    int my_y = top_y + pc_size / 2.0 * blockheight;
    int child_x, child_y, used_y = 0;
    painter->setPen(QPen(Qt::black, 2.0, Qt::SolidLine));
    if (pc->open && !pc->children->isEmpty())
    {
        // Draw line to myself
        //painter->drawLine(my_x - 20, my_y, my_x, my_y);
        for (QList<PartitionCluster *>::Iterator child = pc->children->begin(); child != pc->children->end(); ++child)
        {
            // Draw line from wherever we start to correct height -- actually loop through children since info this side?
            // We are in the middle of these extents at current.x() and current.y() + current.h()
            // Though we may want to take the discreteness of the processes into account and figure it out by blockheight
            painter->setPen(QPen(Qt::black, 2.0, Qt::SolidLine));
            int child_size = (*child)->members->size();
            //std::cout << "  Child size " << child_size << std::endl;
            child_y = top_y + child_size / 2.0 * blockheight + used_y;
            painter->drawLine(my_x, my_y, my_x, child_y);
            //std::cout << "Drawing line from " << my_x << ", " << my_y << "  to   " << my_x << ", " << child_y << std::endl;

            // Draw forward correct amount of px
            child_x = my_x + branch_length;
            painter->drawLine(my_x, child_y, child_x, child_y);

            QRect node = QRect(my_x - 3, my_y - 3, 6, 6);
            painter->fillRect(node, QBrush(Qt::black));
            drawnNodes[pc] = node;

            drawTreeBranch(painter, QRect(child_x, top_y + used_y, current.width(), current.height()), *child,
                                     branch_length, labelwidth, blockheight, leafx);
            used_y += child_size * blockheight;
        }
    }
    else if (pc->children->isEmpty())
    {
        painter->setPen(QPen(Qt::black, 2.0, Qt::SolidLine));
            //std::cout << "Drawing for cluster " << pc->memberString().toStdString().c_str() << std::endl;
            int process = pc->members->at(0);
            //std::cout << "Drawing leaf for member " << process << std::endl;
            painter->drawLine(my_x, my_y, leafx - labelwidth, my_y);
            painter->setPen(QPen(Qt::white, 2.0, Qt::SolidLine));
            painter->drawLine(leafx - labelwidth, my_y, leafx, my_y);
            painter->setPen(QPen(Qt::black, 2.0, Qt::SolidLine));
            painter->drawText(leafx - labelwidth, my_y + 3, QString::number(process));
    }
    else // This is open
    {
        painter->setPen(QPen(Qt::black, 2.0, Qt::SolidLine));
        //std::cout << "Drawing for cluster " << pc->memberString().toStdString().c_str() << std::endl;
        painter->drawLine(my_x, my_y, leafx, my_y);
    }


}

void Gnome::drawGnomeQtClusterBranch(QPainter * painter, QRect current, PartitionCluster * pc,
                                             int blockheight, int blockwidth, int barheight, int barwidth)
{
    //std::cout << "Drawing for cluster " << pc->memberString().toStdString().c_str() << std::endl;
    int pc_size = pc->members->size();
    //std::cout << "PC size is " << pc_size << " and blockheight is " << blockheight << std::endl;
    int my_x = current.x();
    int top_y = current.y();
    int used_y = 0;
    painter->setPen(QPen(Qt::black, 2.0, Qt::SolidLine));
    if (pc->open && !pc->children->isEmpty())
    {
        // Draw line to myself
        //painter->drawLine(my_x - 20, my_y, my_x, my_y);
        for (QList<PartitionCluster *>::Iterator child = pc->children->begin(); child != pc->children->end(); ++child)
        {
            // Draw line from wherever we start to correct height -- actually loop through children since info this side?
            // We are in the middle of these extents at current.x() and current.y() + current.h()
            // Though we may want to take the discreteness of the processes into account and figure it out by blockheight
            int child_size = (*child)->members->size();
            drawGnomeQtClusterBranch(painter, QRect(my_x, top_y + used_y, current.width(), current.height()), *child,
                                     blockheight, blockwidth, barheight, barwidth);
            used_y += child_size * blockheight;
        }
    }
    else if (pc->children->isEmpty())
    {
        int process = pc->members->at(0);
        painter->setPen(QPen(Qt::black, 2.0, Qt::SolidLine));
        drawGnomeQtClusterLeaf(painter, QRect(current.x(), current.y(), barwidth, barheight),
                                  partition->events->value(process), blockwidth, partition->min_global_step);
        drawnPCs[pc] = QRect(current.x(), current.y(), current.width(), blockheight);
        pc->extents = drawnPCs[pc];
    }
    else // This is open
    {
        painter->setPen(QPen(Qt::black, 2.0, Qt::SolidLine));
        QRect clusterRect = QRect(current.x(), current.y(), current.width(), blockheight * pc->members->size());
        drawGnomeQtClusterEnd(painter, clusterRect, pc,
                           barwidth, barheight, blockwidth, blockheight,
                           partition->min_global_step);
        drawnPCs[pc] = clusterRect;
        pc->extents = clusterRect;
    }


}


void Gnome::drawGnomeQtClusterLeaf(QPainter * painter, QRect startxy, QList<Event *> * elist, int blockwidth, int startStep)
{
    int y = startxy.y();
    int x, w, h, xa, wa;
    if (options->showAggregateSteps)
        startStep -= 1;
    painter->setPen(QPen(Qt::black, 2.0, Qt::SolidLine));
    for (QList<Event *>::Iterator evt = elist->begin(); evt != elist->end(); ++evt)
    {
        if (options->showAggregateSteps)
            x = floor(((*evt)->step - startStep) * blockwidth) + 1 + startxy.x();
        else
            x = floor(((*evt)->step - startStep) / 2 * blockwidth) + 1 + startxy.x();
        w = startxy.width();
        h = startxy.height();

        // We know it will be complete in this view because we're not doing scrolling or anything here.

        // Draw the event
        if ((*evt)->hasMetric(metric))
            painter->fillRect(QRectF(x, y, w, h), QBrush(options->colormap->color((*evt)->getMetric(metric))));
        else
            painter->fillRect(QRectF(x, y, w, h), QBrush(QColor(180, 180, 180)));

        // Draw border but only if we're doing spacing, otherwise too messy
        if (blockwidth != w)
            painter->drawRect(QRectF(x,y,w,h));

        if (options->showAggregateSteps) {
            xa = floor(((*evt)->step - startStep - 1) * blockwidth) + 1 + startxy.x();
            wa = startxy.width();

            if ((*evt)->hasMetric(metric))
                painter->fillRect(QRectF(xa, y, wa, h), QBrush(options->colormap->color((*evt)->getMetric(metric, true))));
            else
                painter->fillRect(QRectF(xa, y, wa, h), QBrush(QColor(180, 180, 180)));
            if (blockwidth != w)
                painter->drawRect(QRectF(xa, y, wa, h));
        }

        for (QVector<Message *>::Iterator msg = (*evt)->messages->begin(); msg != (*evt)->messages->end(); ++msg)
            saved_messages.insert(*msg);

    }
}

void Gnome::handleDoubleClick(QMouseEvent * event)
{
    int x = event->x();
    int y = event->y();
    for (QMap<PartitionCluster *, QRect>::Iterator p = drawnPCs.begin(); p != drawnPCs.end(); ++p)
        if (p.value().contains(x,y))
        {
            PartitionCluster * pc = p.key();
            if ((Qt::ControlModifier && event->modifiers()) && (event->button() == Qt::RightButton))
            {
                std::cout << "Top by centroid" << std::endl;
                options->topByCentroid = true;
                generateTopProcesses(pc);
            }
            else if (Qt::ControlModifier && event->modifiers())
            {
                std::cout << "Top by max" << std::endl;
                options->topByCentroid = false;
                generateTopProcesses(pc);
            }
            else
                pc->open = true;
            return;
        }
}

void Gnome::handleTreeDoubleClick(QMouseEvent * event)
{
    int x = event->x();
    int y = event->y();
    std::cout << "Clicked was " << x << ", " << y << std::endl;
    // Figure out which branch this occurs in, open that branch
    for (QMap<PartitionCluster *, QRect>::Iterator p = drawnNodes.begin(); p != drawnNodes.end(); ++p)
    {
        std::cout << "Test is " << p.value().x() << ", " << p.value().y() << std::endl;
        if (p.value().contains(x,y))
        {
            PartitionCluster * pc = p.key();
            pc->close();
            return; // Return so we don't look elsewhere.
        }
    }
}

void Gnome::drawGnomeQtClusterEnd(QPainter * painter, QRect clusterRect, PartitionCluster * pc,
                                  int barwidth, int barheight, int blockwidth, int blockheight,
                                  int startStep)
{
    // Here blockheight is the maximum height afforded to the block. We actually scale it
    // based on how many processes are sending or receiving at that point
    // The first row is send, the second row is recv
    bool drawMessages = true;
    if (clusterRect.height() > 2 * clusterMaxHeight)
    {
        blockheight = clusterMaxHeight - 25;
        barheight = blockheight - 3;
    }
    else
    {
        blockheight = clusterRect.height() / 2;
        if (blockheight < 40)
            drawMessages = false;
        else
            blockheight -= 20; // For message drawing
        if (barheight > blockheight - 3)
            barheight = blockheight;
    }
    int base_y = clusterRect.y() + clusterRect.height() / 2 - blockheight;
    int x, ys, yr, w, hs, hr, xa, wa, ya, ha, nsends, nrecvs;
    yr = base_y + blockheight;
    int base_ya = base_y + blockheight / 2 + (blockheight - barheight) / 2;
    if (options->showAggregateSteps) {
        startStep -= 1;
    }
    //std::cout << "Drawing background " << clusterRect.x() << ", " << clusterRect.y();
    //std::cout << ", " << clusterRect.width() << ", " << clusterRect.height() << std::endl;
    if (alternation) {
        painter->fillRect(clusterRect, QBrush(QColor(217, 217, 217)));
    } else {
        painter->fillRect(clusterRect, QBrush(QColor(189, 189, 189)));
    }
    alternation = !alternation;
    painter->setPen(QPen(Qt::black, 2.0, Qt::SolidLine));
    for (QList<ClusterEvent *>::Iterator evt = pc->events->begin(); evt != pc->events->end(); ++evt)
    {
        if (options->showAggregateSteps)
            x = floor(((*evt)->step - startStep) * blockwidth) + 1 + clusterRect.x();
        else
            x = floor(((*evt)->step - startStep) / 2 * blockwidth) + 1 + clusterRect.x();
        w = barwidth;
        nsends = (*evt)->getCount(ClusterEvent::COMM, ClusterEvent::SEND);
        nrecvs = (*evt)->getCount(ClusterEvent::COMM, ClusterEvent::RECV);
        hs = barheight * nsends / 1.0 / pc->members->size();
        ys = base_y + barheight - hs;
        hr = barheight * nrecvs / 1.0 / pc->members->size();

        // Draw the event
        if ((*evt)->getCount(ClusterEvent::COMM, ClusterEvent::SEND, ClusterEvent::ALL))
            painter->fillRect(QRectF(x, ys, w, hs), QBrush(options->colormap->color(
                                                         (*evt)->getMetric(ClusterEvent::COMM, ClusterEvent::SEND,
                                                                           ClusterEvent::ALL)
                                                         / (*evt)->getCount(ClusterEvent::COMM, ClusterEvent::SEND,
                                                                            ClusterEvent::ALL)
                                                         )));
        if ((*evt)->getCount(ClusterEvent::COMM, ClusterEvent::RECV, ClusterEvent::ALL))
            painter->fillRect(QRectF(x, yr, w, hr), QBrush(options->colormap->color(
                                                         (*evt)->getMetric(ClusterEvent::COMM, ClusterEvent::RECV,
                                                                           ClusterEvent::ALL)
                                                         / (*evt)->getCount(ClusterEvent::COMM, ClusterEvent::RECV,
                                                                            ClusterEvent::ALL)
                                                         )));

        // Draw border but only if we're doing spacing, otherwise too messy
        if (blockwidth != w) {
            painter->drawRect(QRectF(x,ys,w,hs));
            painter->drawRect(QRectF(x,yr,w,hr));
        }

        if (drawMessages) {
            if (nsends)
            {
                painter->setPen(QPen(Qt::black, nsends * 2.0 / pc->members->size(), Qt::SolidLine));
                painter->drawLine(x + blockwidth / 2, ys, x + barwidth, ys - 20);
            }
            if (nrecvs)
            {
                painter->setPen(QPen(Qt::black, nrecvs * 2.0 / pc->members->size(), Qt::SolidLine));
                painter->drawLine(x + blockwidth / 2, yr + barheight, x + 1, yr + barheight + 20);
            }
        }

        if (options->showAggregateSteps) {
            xa = floor(((*evt)->step - startStep - 1) * blockwidth) + 1 + clusterRect.x();
            wa = barwidth;
            ha = barheight * (nsends + nrecvs) / 1.0 / pc->members->size();
            ya = base_ya + (barheight - ha) / 2;

            painter->fillRect(QRectF(xa, ya, wa, ha),
                              QBrush(options->colormap->color(
                                                              (*evt)->getMetric(ClusterEvent::AGG, ClusterEvent::BOTH,
                                                                                ClusterEvent::ALL)
                                                              / (*evt)->getCount(ClusterEvent::AGG, ClusterEvent::BOTH,
                                                                                 ClusterEvent::ALL)
                                                              )));
            if (blockwidth != w)
                painter->drawRect(QRectF(xa, ya, wa, ha));
        }


    }
}
